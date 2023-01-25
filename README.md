# Scalable Server

## Purpose


## Installing

### Prerequisites


### Building
cmake -S . -B build
cmake --build build

The compiler can be specified by passing one of the following to cmake:

- -DCMAKE_C_COMPILER="gcc" -DCMAKE_CXX_COMPILER="g++"
- -DCMAKE_C_COMPILER="clang" -DCMAKE_CXX_COMPILER="clang++"

### Running


### Environment Variables

## Features

### Built-in Commands

### Limitations

## Examples
./scalable_server -c 127.0.0.1 < a.txt