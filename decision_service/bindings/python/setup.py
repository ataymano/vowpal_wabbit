import glob
import setuptools

with open("README.md", "r") as fh:
	long_description = fh.read()

extension_module = setuptools.Extension(
	'rlinference._rlinference',
	sources = glob.glob('*.cc'),
	library_dirs = ['../../rlclientlib/', '../../../vowpalwabbit/'],
	include_dirs = ['../../include/'],
	libraries = ['rlclient', 'vw', 'allreduce', 'boost_system', 'boost_program_options', 'cpprest', 'ssl', 'crypto', 'z', 'pthread', 'dl'],
	extra_compile_args = ['-std=c++11'],
)

setuptools.setup(
	name = "rlinference",
	version = "0.0.3",
	author = "Microsoft",
	author_email = "email?",
	description = "Python binding for reinforcement learning client library",
	long_description = long_description,
	url = "url?",
	ext_modules = [extension_module],
	py_modules = ['rlinference.py'],
	packages = setuptools.find_packages(),
	classifiers = (
		"Programming Language :: Python :: 3",
		"Operating System :: OS Independent"
	)
)

