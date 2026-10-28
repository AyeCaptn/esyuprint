.PHONY: build run check check-all test format package install
build:
	bash bin/build
run: build
	./build/esyuprint open
check:
	bash scripts/check.sh
check-all:
	bash scripts/check-all.sh
test:
	bash scripts/test.sh $(ARGS)
format:
	bash scripts/format.sh
package: build
	cd pkgbuild && makepkg -f
install:
	bash bin/install
