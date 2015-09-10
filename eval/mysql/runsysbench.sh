

Running:
# First step: prepare the sbtest table
sysbench --test=oltp --mysql-user=mysql --db-driver=mysql --mysql-db=test \
  --mysql-socket=/var/lib/mysql/mysql.sock --mysql-host=localhost --mysql-table-engine=innodb prepare

#sysbench --test=oltp --mysql-user=root --mysql-password=cosmobic --db-driver=mysql --mysql-db=test \
#  --mysql-host=localhost --mysql-table-engine=innodb prepare

#exit; 
# Run the test.
time perl -e "foreach(1..2){print \`sysbench --max-requests=1000 --test=oltp \
  --mysql-user=mysql --db-driver=mysql --mysql-socket=/var/lib/mysql/mysql.sock --mysql-db=test --mysql-host=localhost \
  --mysql-table-engine=innodb --num-threads=100 run\`}"

