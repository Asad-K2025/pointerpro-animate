# You may modify this file as you wish, but `make animate.o` should always
# compile all necessary code into a single object file.

default: animate.o test_simple

test_simple: main_simple.c animate.o | animate.h
	gcc $^ -o $@

animate.o: animate.c | animate.h
	gcc -fPIC -c $^ -o $@




API_DOC=PointerProAnimateRefman.pdf
doc: $(API_DOC)

# Generates a doxygen configuration file
Doxyfile:
	doxygen -g
	# Adjust params
	sed -i 's/\(GENERATE_HTML *= *\).*/\1NO/g' $@
	sed -i 's/\(EXTRACT_ALL *= *\).*/\1YES/g' $@
	sed -i 's/\(INPUT *= *\).*/\1"animate.h"/g' $@
	sed -i 's/\(PROJECT_NAME *= *\).*/\1"PointerPro Animate"/g' $@
	sed -i 's/\(OPTIMIZE_OUTPUT_FOR_C *= *\).*/\1YES/g' $@

$(API_DOC): DOC_MAKEFILE=latex/Makefile
$(API_DOC): DOC_TOP=latex/refman.tex
$(API_DOC): Doxyfile | animate.h
	doxygen $^
	# Don't index
	sed -i 's/\t$$(MKIDX/\t#/g'             $(DOC_MAKEFILE)
	# Remove some dead chapters and ToC
	sed -i 's/^ *\\clearemptydoublepage//g' $(DOC_TOP)
	sed -i 's/^ *\\tableofcontents//g'      $(DOC_TOP)
	sed -i 's/^ *\\chapter{File Index}//g'  $(DOC_TOP)
	sed -i 's/^ *\\input{files}//g'         $(DOC_TOP)
	cd latex && make
	cp latex/refman.pdf $@

clean:
	rm -f animate.o
	rm -f Doxyfile
	rm -rf latex

clobber: clean
	rm -f $(API_DOC)

.PHONY: doc clean clobber default
