#!/bin/bash

#Download mysql-5.5.32.tar.gz
wget https://downloads.mariadb.com/archives/mysql-5.5/mysql-5.5.32.tar.gz
tar zxvf mysql-5.5.32.tar.gz
cd mysql-5.5.32
cmake .
make clean
make

#./bin/mysqld_safe &
