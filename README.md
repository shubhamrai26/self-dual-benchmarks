# iwls2020_experiments

# Usage
```sh
git clone https://github.com/lsils/mockturtle
cd mockturtle
mv experiments experiments.bak
git clone https://github.com/shubhamrai26/iwls2020_experiments.git experiments
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=RELEASE -DMOCKTURTLE_EXPERIMENTS=ON ..
make -j4
```
