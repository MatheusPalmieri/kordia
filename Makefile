PY ?= python

interface:
	$(PY) tasks.py interface

test:
	$(PY) tasks.py test

avaliar:
	$(PY) tasks.py avaliar $(VERSAO)

gerar:
	$(PY) tasks.py gerar

.PHONY: interface test avaliar gerar
