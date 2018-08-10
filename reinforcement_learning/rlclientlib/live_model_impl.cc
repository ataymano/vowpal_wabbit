#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include "utility/context_helper.h"
#include "logger/logger.h"
#include "api_status.h"
#include "configuration.h"
#include "error_callback_fn.h"
#include "ranking_response.h"
#include "live_model_impl.h"
#include "ranking_event.h"
#include "err_constants.h"
#include "constants.h"
#include "vw_model/safe_vw.h"
#include "explore_internal.h"
#include "hash.h"

// Some namespace changes for more concise code
namespace e = exploration;
using namespace std;

const uint64_t a_hack = 0xeece66d5deece66dULL;
const uint64_t c_hack = 2147483647;

const int bias_hack = 127 << 23;

union int_float_hack
{
  int32_t i;
  float f;
};

inline float uniform_random_merand48_hack(uint64_t& initial)
{
  initial = a_hack * initial + c_hack;
  int_float_hack temp;
  temp.i = ((initial >> 25) & 0x7FFFFF) | bias_hack;
  return temp.f - 1;
}

int uniform_int_hack(uint64_t seed, uint64_t low, uint64_t high)
{
  uniform_random_merand48_hack(seed);
  return low + (uint64_t)((seed >> 25) % (high - low + 1));
}

namespace reinforcement_learning {
  // Some namespace changes for more concise code
  namespace m = model_management;
  namespace u = utility;

  // Some typdefs for more concise code
  using vw_ptr = std::shared_ptr<safe_vw>;
  using pooled_vw = utility::pooled_object_guard<safe_vw, safe_vw_factory>;

  int check_null_or_empty(const char* arg1, const char* arg2, api_status* status);
  int check_null_or_empty(const char* arg1, api_status* status);

  int live_model_impl::init(api_status* status) {
    int scode = _logger.init(status);
    RETURN_IF_FAIL(scode);
    scode = init_model(status);
    RETURN_IF_FAIL(scode);
    scode = init_model_mgmt(status);
    RETURN_IF_FAIL(scode);
    _initial_epsilon = _configuration.get_float(name::INITIAL_EPSILON, 0.2f);
    const char* app_id = _configuration.get(name::APP_ID, "");
    _seed_shift = uniform_hash(app_id, strlen(app_id), 0);
    return scode;
  }

  int live_model_impl::choose_rank(const char* event_id, const char* context, ranking_response& response,
                                   api_status* status) {
    response.clear();
    //clear previous errors if any
    api_status::try_clear(status);
    //check arguments
    RETURN_IF_FAIL(check_null_or_empty(event_id, context, status));
    int scode;
    if (!_model_data_received) {
      scode = explore_only(event_id, context, response, status);
      RETURN_IF_FAIL(scode);
      response.set_model_id("N/A");
    }
    else {
      scode = explore_exploit(event_id, context, response, status);
      RETURN_IF_FAIL(scode);
    }
    response.set_event_id(event_id);
    // Serialize the event
    u::pooled_object_guard<u::data_buffer, u::buffer_factory> guard(_buffer_pool, _buffer_pool.get_or_create());
    guard->reset();
    ranking_event::serialize(*guard.get(), event_id, context, response);
    auto sbuf = guard->str();
    // Send the ranking event to the backend
    RETURN_IF_FAIL(_logger.append_ranking(sbuf, status));
    return error_code::success;
  }

  //here the event_id is auto-generated
  int live_model_impl::choose_rank(const char* context, ranking_response& response, api_status* status) {
    return choose_rank(boost::uuids::to_string(boost::uuids::random_generator()()).c_str(), context, response,
                       status);
  }

  int live_model_impl::report_outcome(const char* event_id, const char* outcome, api_status* status) {
    // Check arguments
    RETURN_IF_FAIL(check_null_or_empty(event_id, outcome, status));
    return report_outcome_internal(event_id, outcome, status);
  }

  int live_model_impl::report_outcome(const char* event_id, float outcome, api_status* status) {
    // Check arguments
    RETURN_IF_FAIL(check_null_or_empty(event_id, status));
    return report_outcome_internal(event_id, outcome, status);
  }

  live_model_impl::live_model_impl(
    const utility::configuration& config,
    const error_fn fn,
    void* err_context,
    transport_factory_t* t_factory,
    model_factory_t* m_factory
  )
    : _configuration(config),
      _error_cb(fn, err_context),
      _data_cb(_handle_model_update, this),
      _logger(config, &_error_cb),
      _t_factory{t_factory},
      _m_factory{m_factory},
      _transport(nullptr),
      _model(nullptr),
      _model_download(nullptr),
      _bg_model_proc(config.get_int(name::MODEL_REFRESH_INTERVAL_MS, 60 * 1000), &_error_cb),
      _buffer_pool(new u::buffer_factory(utility::translate_func('\n', ' '))) { }

  int live_model_impl::init_model(api_status* status) {
    const auto model_impl = _configuration.get(name::MODEL_IMPLEMENTATION, value::VW);
    m::i_model* pmodel;
    RETURN_IF_FAIL(_m_factory->create(&pmodel, model_impl, _configuration,status));
    _model.reset(pmodel);
    return error_code::success;
  }

  void inline live_model_impl::_handle_model_update(const m::model_data& data, live_model_impl* ctxt) {
    ctxt->handle_model_update(data);
  }

  void live_model_impl::handle_model_update(const model_management::model_data& data) {
    api_status status;
    if (_model->update(data, &status) != error_code::success) {
      _error_cb.report_error(status);
      return;
    }
    _model_data_received = true;
  }

  int live_model_impl::explore_only(const char* event_id, const char* context, ranking_response& response,
                                    api_status* status) const {
    // Generate egreedy pdf
    size_t action_count = 0;
    RETURN_IF_FAIL(utility::get_action_count(action_count, context, status));
    vector<float> pdf(action_count);
    // Assume that the user's top choice for action is at index 0
    const auto top_action_id = 0;
    auto scode = e::generate_epsilon_greedy(_initial_epsilon, top_action_id, begin(pdf), end(pdf));
    if (S_EXPLORATION_OK != scode) {
      RETURN_ERROR_LS(status, exploration_error) << "Exploration error code: " << scode;
    }
    // Pick using the pdf
    uint32_t chosen_action_id;
    const uint64_t seed = uniform_hash(event_id, strlen(event_id), 0) + _seed_shift;
    chosen_action_id = uniform_int_hack(seed, 0, action_count - 1);
    if (S_EXPLORATION_OK != scode) {
      RETURN_ERROR_LS(status, exploration_error) << "Exploration error code: " << scode;
    }
    response.push_back(chosen_action_id, pdf[chosen_action_id]);
    // Setup response with pdf from prediction and chosen action
    for (size_t idx = 1; idx < pdf.size(); ++idx) {
      const auto cur_idx = chosen_action_id != idx ? idx : 0;
      response.push_back(cur_idx, pdf[cur_idx]);
    }
    response.set_chosen_action_id(chosen_action_id);
    return error_code::success;
  }

  int live_model_impl::explore_exploit(const char* event_id, const char* context, ranking_response& response,
                                       api_status* status) const {
    const uint64_t seed = uniform_hash(event_id, strlen(event_id), 0) + _seed_shift;
    return _model->choose_rank(seed, context, response, status);
  }

  int live_model_impl::init_model_mgmt(api_status* status) {
    // Initialize transport for the model using transport factory
    const auto tranport_impl = _configuration.get(name::MODEL_SRC, value::AZURE_STORAGE_BLOB);
    m::i_data_transport* ptransport;
    RETURN_IF_FAIL(_t_factory->create(&ptransport, tranport_impl, _configuration, status));
    // This class manages lifetime of transport
    this->_transport.reset(ptransport);
    // Initialize background process and start downloading models
    this->_model_download.reset(new m::model_downloader(ptransport, &_data_cb));
    return _bg_model_proc.init(_model_download.get(), status);
  }

  //helper: check if at least one of the arguments is null or empty
  int check_null_or_empty(const char* arg1, const char* arg2, api_status* status) {
    if (!arg1 || !arg2 || strlen(arg1) == 0 || strlen(arg2) == 0) {
      api_status::try_update(status, error_code::invalid_argument,
                             "one of the arguments passed to the ds is null or empty");
      return error_code::invalid_argument;
    }
    return error_code::success;
  }

  int check_null_or_empty(const char* arg1, api_status* status) {
    if (!arg1 || strlen(arg1) == 0) {
      api_status::try_update(status, error_code::invalid_argument,
                             "one of the arguments passed to the ds is null or empty");
      return error_code::invalid_argument;
    }
    return error_code::success;
  }

}
