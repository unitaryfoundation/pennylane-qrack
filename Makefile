PYTHON3 := $(shell which python3 2>/dev/null)

PYTHON := python3
COVERAGE := --cov=pennylane_qrack --cov-report term-missing --cov-report=html:coverage_html_report
TESTRUNNER := -m pytest tests

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)
QRACK_PRESENT := $(wildcard qrack/.)

ifeq ("$(wildcard /usr/local/bin/cmake)", "/usr/local/bin/cmake")
CMAKE_L := /usr/local/bin/cmake
else
ifeq ("$(wildcard /usr/bin/cmake)", "/usr/bin/cmake")
CMAKE_L := /usr/bin/cmake
else
CMAKE_L := cmake
endif
endif

.PHONY: help
help:
	@echo "Please use \`make <target>' where <target> is one of"
	@echo "  build-deps         to build PennyLane-Qrack C++ dependencies"
	@echo "  install            to install PennyLane-Qrack"
	@echo "  wheel              to build the PennyLane-Qrack wheel"
	@echo "  dist               to package the source distribution"
	@echo "  clean              to delete all temporary, cache, and build files"
	@echo "  clean-docs         to delete all built documentation"
	@echo "  test               to run the test suite"
	@echo "  coverage           to generate a coverage report"

.PHONY: build-deps
build-deps:
ifneq ($(OS),Windows_NT)
ifeq ($(QRACK_PRESENT),)
	git clone https://github.com/unitaryfund/qrack.git; cd qrack; git checkout 4154230f4e6ccbaf44d1708a5aff59a578ab7119; cd ..
endif
	mkdir -p qrack/build
ifeq ($(UNAME_S),Linux)
ifneq ($(filter $(UNAME_P),x86_64 i386),)
	cd qrack/build; $(CMAKE_L) -DCPP_STD=20 -DENABLE_RDRAND=OFF -DENABLE_DEVRAND=ON -DQBCAPPOW=8 ..; make qrack; cd ../..
else
	cd qrack/build; $(CMAKE_L) -DCPP_STD=20 -DENABLE_RDRAND=OFF -DENABLE_DEVRAND=ON -DENABLE_COMPLEX_X2=OFF -DENABLE_SSE3=OFF -DQBCAPPOW=8 ..; make qrack; cd ../..
endif
	mkdir -p _qrack_include/qrack; cp -r qrack/include/* _qrack_include/qrack; cp -r qrack/build/include/* _qrack_include/qrack; mkdir -p _build; cd _build; $(CMAKE_L) ..; make all; cd ..; cp _build/libqrack_device.so pennylane_qrack/
endif
ifeq ($(UNAME_S),Darwin)
ifneq ($(filter $(UNAME_P),x86_64 i386),)
	cd qrack/build; $(CMAKE_L) -DCPP_STD=20 -DENABLE_OPENCL=OFF -DQBCAPPOW=8 -DBoost_INCLUDE_DIR=/opt/homebrew/include -DBoost_LIBRARY_DIRS=/opt/homebrew/lib ..; make install; cd ../..
else
	cd qrack/build; $(CMAKE_L) -DCPP_STD=20 -DENABLE_OPENCL=OFF -DENABLE_RDRAND=OFF -DENABLE_COMPLEX_X2=OFF -DENABLE_SSE3=OFF -DQBCAPPOW=8 -DBoost_INCLUDE_DIR=/opt/homebrew/include -DBoost_LIBRARY_DIRS=/opt/homebrew/lib ..; make install; cd ../..
endif
endif
endif

.PHONY: install
install:
ifndef PYTHON3
	@echo "To install PennyLane-Qrack you need to have Python 3 installed"
endif
	$(PYTHON) setup.py install

.PHONY: wheel
wheel:
	$(PYTHON) setup.py bdist_wheel

.PHONY: dist
dist:
	$(PYTHON) setup.py sdist

.PHONY : clean
clean:
	rm -rf pennylane_qrack/__pycache__
	rm -rf tests/__pycache__
	rm -rf dist
	rm -rf build
	rm -rf .pytest_cache
	rm -rf .coverage coverage_html_report/

docs:
	make -C doc html

.PHONY : clean-docs
clean-docs:
	make -C doc clean


test:
	$(PYTHON) $(TESTRUNNER)

coverage:
	@echo "Generating coverage report..."
	$(PYTHON) $(TESTRUNNER) $(COVERAGE)
