.PHONY: check cross mutations plan
PYTHON ?= python3
check:
	$(PYTHON) tools/check.py --sanitize --cross
cross:
	$(PYTHON) tools/cross_abi.py
mutations:
	$(PYTHON) tools/check_mutations.py
plan:
	$(PYTHON) tools/lab.py validate
	$(PYTHON) tools/lab.py plan
