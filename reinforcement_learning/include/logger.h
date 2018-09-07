#pragma once

#include <string>

namespace reinforcement_learning {
  class api_status;

  class i_logger {
  public:
    virtual int init(api_status* status) = 0;

    //! In order to avoid having default parameters in the pure virtual function, wrap it in this call.
    int append(std::string& data, api_status* status = nullptr)
    {
      return v_append(data, status);
    }

    virtual bool is_full() {
      return false;
    }

    virtual ~i_logger() = default;
    protected:
      virtual int v_append(std::string& data, api_status* status) = 0;
  };
}
