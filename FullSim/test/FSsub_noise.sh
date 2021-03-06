#!/bin/bash

KEEPTAR=$1
FPRE=fullsimnoiseanalyzer
NUM=500
PART=200
ENERGY=30
JOBDIR=batch
OUTDIR=root://cmseos.fnal.gov//store/user/pedrok/raddam/tree

./FScheck.sh "$KEEPTAR" "$JOBDIR"

for YEAR in 17 19
  do
    for LUMI in 0 50 100 150 300 500
      do
        ./FStemp.sh ${FPRE} ${JOBDIR} ${OUTDIR} ${YEAR} ${ENERGY} ${LUMI} ${NUM} ${PART}
      done
  done
