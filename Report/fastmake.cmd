set texfile=report
set aux-dir=aux-files

if not exist %aux-dir% goto createAuxDir
goto make

:createAuxDir
md %aux-dir%
goto make

:make

tskill AcroRd32

pdflatex %texfile% -aux-directory=%aux-dir%
rem bibtex %aux-dir%/%texfile%
rem pdflatex %texfile%.tex -aux-directory=%aux-dir% > aux-files/latex_printout.log
rem pdflatex %texfile%.tex -aux-directory=%aux-dir% > aux-files/latex_printout.log

start %texfile%.pdf