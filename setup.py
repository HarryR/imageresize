#!/usr/bin/env python
from distutils.core import setup, Extension

setup(name = "pyirz",
	version ='0.1',
	description='Python Interface for imageresize',
	author='Harry Roberts',
	author_email='pyirz@midnight-labs.org',
	url='https://github.com/HarryR/imageresize',
	ext_package = 'pyirz',
	ext_modules = [Extension("pyirz", ["pyirz.c",'irz.c'],
	language = 'c',
	libraries = ['jpeg'])])
