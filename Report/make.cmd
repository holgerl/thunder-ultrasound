set texfile=report
set aux-dir=aux-files

if not exist %aux-dir% goto createAuxDir
del %aux-dir% /Q
goto make

:createAuxDir
md %aux-dir%
goto make

:make

tskill AcroRd32

pdflatex %texfile% -aux-directory=%aux-dir%
bibtex %aux-dir%/%texfile%
pdflatex %texfile%.tex -aux-directory=%aux-dir% > aux-files/latex_printout.log
pdflatex %texfile%.tex -aux-directory=%aux-dir% > aux-files/latex_printout.log

start %texfile%.pdf