CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -g
LDFLAGS ?=

MLWSVM  ?= $(shell if [ -x ../mlws-vm/mlwsvm ]; then echo ../mlws-vm/mlwsvm; else command -v mlwsvm || echo mlwsvm; fi)

SRCS := src/main.c src/util.c src/lexer.c src/ast.c src/parser.c src/sema.c src/codegen.c
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

.PHONY: all clean test test-ollama check-examples

all: ail

build:
	mkdir -p build

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

ail: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

test: ail
	MLWSVM="$(MLWSVM)" sh tests/run_tests.sh

test-ollama: ail
	MLWSVM="$(MLWSVM)" ./ail run examples/agent_ollama.ail --env examples/ollama.ail.env
	MLWSVM="$(MLWSVM)" ./ail run examples/flow_ollama.ail --env examples/ollama_v03.ail.env
	MLWSVM="$(MLWSVM)" ./ail run examples/stream_ollama.ail --env examples/ollama_v03.ail.env

check-examples: ail
	@for f in examples/*.ail examples/support_desk/main.ail; do \
		echo "CHECK $$f"; \
		./ail check "$$f" || exit 1; \
	done

clean:
	rm -rf build ail
