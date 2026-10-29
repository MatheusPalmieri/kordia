PY ?= python

test:
	$(PY) tasks.py test

avaliar:
	$(PY) tasks.py avaliar $(VERSAO)

gerar:
	$(PY) tasks.py gerar

.PHONY: test avaliar gerar
