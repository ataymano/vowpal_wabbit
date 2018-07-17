import glob
import setuptools

with open("README.md", "r") as fh:
	long_description = fh.read()

extension_module = setuptools.Extension(
	'rlinference.extension',
	sources = glob.glob('*_wrap.cc'),
	library_dirs = ['../../../rlclientlib/'],
	include_dirs = ['../../../include/'],
	libraries = ['rlclient']
)

setuptools.setup(
	name = "rlinference",
	version = "0.0.2",
	author = "Alexey Taymanov",
	author_email = "ataymano@gmail.com",
	description = "A small example",
	long_description = long_description,
	long_description_content_type = "text/markdown",
	url = "https://github.com/ataymano/hello_world_12.git",
	ext_modules = [extension_module],
	packages = setuptools.find_packages(),
	classifiers = (
		"Programming Language :: Python :: 3",
		"License :: OSI Approved :: MIT License",
		"Operating System :: OS Independent"
	)
)

