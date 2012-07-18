July 2012

-------------------
GENERAL INFORMATION
-------------------

This is version 1.0 of Nenofex ("NEgation NOrmal Form EXpansion"), an
expansion-based QBF solver which operates on negation normal form (NNF). A
formula in NNF is represented as a structurally restricted tree. Expansions of
variables from the two rightmost quantifier blocks are scheduled based on
estimated expansion costs. Further information can be found in our SAT'08
paper:

@inproceedings{DBLP:conf/sat/LonsingB08,
  author    = {Florian Lonsing and
               Armin Biere},
  title     = {Nenofex: Expanding NNF for QBF Solving},
  booktitle = {SAT},
  year      = {2008},
  pages     = {196-210},
  ee        = {http://dx.doi.org/10.1007/978-3-540-79719-7_19},
  crossref  = {DBLP:conf/sat/2008},
  bibsource = {DBLP, http://dblp.uni-trier.de}
}

Nenofex is no longer actively developed. For bug reports, please contact
Florian Lonsing (see below).


------------
INSTALLATION
------------

Create a new directory DIR.

Download a recent source package of PicoSAT from
http://fmv.jku.at/picosat/ copy it to DIR and unpack it. (Installation
should work with 'picosat-936.tar.gz', for example)

In the directory of PicoSAT, call './configure -O -static' and then
'make'.

Make sure that the directory of PicoSAT is named 'picosat'. Rename it
if necessary.

Copy the source package of Nenofex to directory DIR and unpack it. The
directory tree should now look like 'DIR/picosat' and 'DIR/nenofex'. Call
'make' in the directory of Nenofex which produces optimized code without
assertions. The compilation process of Nenofex requires to have PicoSAT
compiled before in directory 'DIR/picosat/'.


-----------------------
CONFIGURATION AND USAGE
-----------------------

Call './nenofex -h' to display usage information. Calling Nenofex without
command line parameters results in default behaviour.

The solver returns exit code 10 if the given instance was found satisfiable
and exit code 20 if the instance was found unsatisfiable. Any other exit code
indicates that the instance was not solved.


-------
CONTACT
-------

For comments, questions, bug reports etc. related to Nenofex please contact
Florian Lonsing.

See also http://www.kr.tuwien.ac.at/staff/lonsing/


