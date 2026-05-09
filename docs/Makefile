prefix=rxnet-

pdf: $(prefix)specs-c.pdf $(prefix)specs-python.pdf $(prefix)user-python.pdf $(prefix)user-c.pdf

PANDOC_FLAGS = \
	--from markdown \
	--template eisvogel \
	-M date="$(shell date +%Y-%m-%d)" \
	-M titlepage=true \
	-M titlepage-rule-height=2 \
	-M listings=true \
	-M book=true \
	--syntax-highlighting idiomatic \
	--toc \
	--toc-depth=3 \
	--lof \
	-V caption-justification=centering \
	-V documentclass=report \
	--top-level-division=chapter \
	--filter pandoc-crossref \
	--filter mermaid-filter \
	--pdf-engine=xelatex

$(prefix)specs-c.pdf: specs-c.md specs/c/requirements.md specs/c/design.md specs/c/tasks.md
	@echo "Generating $@..."
	@pandoc $^ -o $@ $(PANDOC_FLAGS)

$(prefix)specs-python.pdf: specs-python.md specs/python/requirements.md specs/python/design.md specs/python/tasks.md
	@echo "Generating $@..."
	@pandoc $^ -o $@ $(PANDOC_FLAGS)

$(prefix)user-python.pdf: user-intro.md user-python.md
	@echo "Generating $@..."
	@pandoc $^ -o $@ $(PANDOC_FLAGS) \
		-M title="rxnet — Guía de usuario: Python" \
		-M subtitle="FSM y Redes de Petri sobre un runtime síncrono reactivo"

$(prefix)user-c.pdf: user-intro.md user-c.md
	@echo "Generating $@..."
	@pandoc $^ -o $@ $(PANDOC_FLAGS) \
		-M title="rxnet — Guía de usuario: C" \
		-M subtitle="FSM y Redes de Petri sobre un runtime síncrono reactivo"

clean:
	rm -f $(prefix)specs-c.pdf $(prefix)specs-python.pdf $(prefix)user-python.pdf $(prefix)user-c.pdf
