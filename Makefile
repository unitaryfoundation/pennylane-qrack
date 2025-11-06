PYTHON3 := $(shell which python3 2>/dev/null)

PYTHON := python3
COVERAGE := --cov=pennylane_qrack --cov-report term-missing --cov-report=html:coverage_html_report
TESTRUNNER := -m pytest tests

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)
QRACK_PRESENT := $(wildcard qrack/.)

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
	git clone https://github.com/unitaryfund/qrack.git; cd qrack; git checkout 944f418dd5ac4b37f0af20a1ec9e8903ec5e2717; cd ..
endif
	mkdir -p qrack/build
ifeq ($(UNAME_S),Linux)
ifeq ($(UNAME_P),x86_64)
	cd qrack/build; cmake -DENABLE_RDRAND=OFF -DENABLE_DEVRAND=ON -DQBCAPPOW=11 ..; make qrack; cd ../..
else
	cd qrack/build; cmake -DENABLE_RDRAND=OFF -DENABLE_DEVRAND=ON -DENABLE_COMPLEX_X2=OFF -DENABLE_SSE3=OFF -DQBCAPPOW=11 ..; make qrack; cd ../..
endif
endif
ifeq ($(UNAME_S),Darwin)
ifeq ($(UNAME_P),x86_64)
	cd qrack/build; cmake -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++  -DENABLE_RDRAND=OFF -DQBCAPPOW=11 -DCPP_STD=14 -DBoost_INCLUDE_DIR=/opt/homebrew/include -DBoost_LIBRARY_DIRS=/opt/homebrew/lib ..; make qrack; cd ../..
else
	cd qrack/build; cmake -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++ -DENABLE_OPENCL=OFF -DENABLE_COMPLEX_X2=OFF -DENABLE_SSE3=OFF -DENABLE_RDRAND=OFF -DQBCAPPOW=11 -DCPP_STD=14 -DBoost_INCLUDE_DIR=/opt/homebrew/include -DBoost_LIBRARY_DIRS=/opt/homebrew/lib ..; make qrack; cd ../..
endif
endif
endif
ifeq ($(UNAME_S),Darwin)
        rm -rf _qrack_include; mkdir _qrack_include; mkdir _qrack_include/qrack; cp -r qrack/include/* _qrack_include/qrack; cp -r qrack/build/include/* _qrack_include/qrack; \
	cd pennylane_qrack; cmake -DCMAKE_CXX_COMPILER=/opt/homebrew/bin/g++ ..; make all
else
	rm -rf _qrack_include; mkdir _qrack_include; mkdir _qrack_include/qrack; cp -r qrack/include/* _qrack_include/qrack; cp -r qrack/build/include/* _qrack_include/qrack; \
	cd pennylane_qrack; cmake ..; make all
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
