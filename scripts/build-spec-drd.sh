#!/bin/bash

# Part of the Data Race Performance Benchmark, under the Apache License v2.0.
# See LICENSE for license information.
# SPDX-License-Identifier: Apache-2.0

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
export BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )"/.. &> /dev/null && pwd )"

set +e
set -x
SIZE=drd

if [ -e $BASE_DIR/spec/benchspec/OMP2012/350.md/data/$SIZE/input/ ]; then
  echo Found drd input in $BASE_DIR/spec
  exit 0
fi
if [ ! -e $BASE_DIR/spec/benchspec/OMP2012/350.md/data/ref/input/ ]; then
  echo Expect Spec OMP 2012 benchmark in $BASE_DIR/spec, did not find ref data
  exit 1
fi
OMP2012_DIR=$BASE_DIR/spec/benchspec/OMP2012/

# 360.ilbdc copy test
cp -ax $OMP2012_DIR/360.ilbdc/data/test/ $OMP2012_DIR/360.ilbdc/data/$SIZE
# 362.fma3d copy train
cp -ax $OMP2012_DIR/362.fma3d/data/train/ $OMP2012_DIR/362.fma3d/data/$SIZE

# 350.md copy train, patch
cp -ax $OMP2012_DIR/350.md/data/train/ $OMP2012_DIR/350.md/data/$SIZE

# 367.imagick
cp -ax $OMP2012_DIR/367.imagick/data/train/ $OMP2012_DIR/367.imagick/data/$SIZE/

TEMP_DIR=$(mktemp -d $BASE_DIR/drd-tmpXXXXX)
cd ${TEMP_DIR}
tar xf $BASE_DIR/Spec-OMP2012-drd.tar.gz
for i in benchspec/OMP2012/*/data ; do
  mkdir -p $BASE_DIR/spec/$i/$SIZE/input
  cp -r $i/drd/* $BASE_DIR/spec/$i/$SIZE
done
rm -rf ${TEMP_DIR}

# 351.bwaves
sed -e's/63      63      128/144     144     144/' $OMP2012_DIR/351.bwaves/data/train/input/bwaves.in > $OMP2012_DIR/351.bwaves/data/$SIZE/input/bwaves.in

# 357.bt331
sed -e's/64 64 64/80 80 80/' $OMP2012_DIR/357.bt331/data/test/input/inputbt.data > $OMP2012_DIR/357.bt331/data/$SIZE/input/inputbt.data

# 358.botsalgn
(echo Number of sequences is 200; head -n 401 $OMP2012_DIR/358.botsalgn/data/ref/input/botsalgn | tail -n400) > $OMP2012_DIR/358.botsalgn/data/$SIZE/input/botsalgn
# 363.swim
sed -e's/1334/1734/' $OMP2012_DIR/363.swim/data/train/input/swim.in > $OMP2012_DIR/363.swim/data/$SIZE/input/swim.in

# 371.applu331
sed -e's/64  64  64/80  80  80/' $OMP2012_DIR/371.applu331/data/train/input/inputlu.data > $OMP2012_DIR/371.applu331/data/$SIZE/input/inputlu.data

# 352.nab
echo "gcn4p1 1850041461" > $OMP2012_DIR/352.nab/data/$SIZE/input/control

# 359.botsspar
echo "botsspar 120 90" > $OMP2012_DIR/359.botsspar/data/$SIZE/input/control

# 367.imagick
echo "convert convert1.out convert1.err 1 -shear 31 -resize 1280x960 -negate -edge 14 -implode 1.2 -flop -convolve 1,2,1,4,3,4,1,2,1 -edge 100 input1.tga output1.tga" > $OMP2012_DIR/367.imagick/data/$SIZE/input/control

# 370.mgrid331
cat > $OMP2012_DIR/370.mgrid331/data/$SIZE/input/mg.input  <<EOT
9 = top level
256 128 1024 = nx ny nz
150 = nit
0 0 0 0 0 0 0 0 = debug_vec
EOT

# 372.smithwa
echo "testset 35" > $OMP2012_DIR/372.smithwa/data/$SIZE/input/control

# 376.kdtree
echo "testset 350000 10 2" > $OMP2012_DIR/376.kdtree/data/$SIZE/input/control
