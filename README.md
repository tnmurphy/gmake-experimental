# gmake-experimental
This repository is aimed at being a home for experimental changes to GNU make which are either not ready to be submitted for inclusion in GNU Make itself or are too radical or too specialised to be considered for inclusion. 

## What's in it now?
Currently it is host to the jprint branch which gives the ability to dump make's internal data structurs after parsing in JSON format thus providing a reliable way to parse makefiles.

## Structure
The master branch is untouched GNU Make (except for this README) and is occasionally updated so that the feature branches may be rebased ontop of a more recent GNU make.

There's no attempt to integrate new features - that could be done at a later date.  Each experimental feature can exist separately to start with.

## Baseline
The current baseline is GNU Make 4.4.90. 

## License 
The license for all code within is the same as for GNU Make itself.