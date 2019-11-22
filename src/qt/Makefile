.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. dogecoin_qt test_dogecoin_qt
clean: FORCE
	$(MAKE) -C .. dogecoin_qt_clean test_dogecoin_qt_clean
check: FORCE
	$(MAKE) -C .. test_dogecoin_qt_check
bitcoin-qt bitcoin-qt.exe: FORCE
	 $(MAKE) -C .. dogecoin_qt
