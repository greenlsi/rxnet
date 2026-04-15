pdf: specs-c.pdf specs-python.pdf

PANDOC_FLAGS = \
	--from markdown \
	--template eisvogel \
	-M date="$(shell date +%Y-%m-%d)" \
	-M titlepage=true \
	-M titlepage-color="006EB7" \
	-M titlepage-text-color="FFFFFF" \
	-M titlepage-rule-color="FFFFFF" \
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

specs-c.pdf: specs-c.md specs/c/requirements.md specs/c/design.md specs/c/tasks.md
	@echo "Generating $@..."
	@pandoc $^ -o $@ $(PANDOC_FLAGS)

specs-python.pdf: specs-python.md specs/python/requirements.md specs/python/design.md specs/python/tasks.md
	@echo "Generating $@..."
	@pandoc $^ -o $@ $(PANDOC_FLAGS)

clean:
	rm -f specs-c.pdf specs-python.pdf
