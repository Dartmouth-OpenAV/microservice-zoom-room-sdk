#!/bin/bash

echo "> initializing memory DB"
sqlite3 /dev/shm/microservice.db << EOF
-- tasks table
CREATE TABLE tasks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device TEXT NOT NULL,
  path TEXT NOT NULL,
  method TEXT NOT NULL,
  datum TEXT DEFAULT NULL,
  added_timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  process_at_timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  in_process TEXT DEFAULT 'false',
  in_process_since_timestamp TIMESTAMP DEFAULT NULL
);

CREATE INDEX idx_tasks_device ON tasks(device);

-- data table
CREATE TABLE IF NOT EXISTS data (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device TEXT NOT NULL,
  path TEXT NOT NULL,
  datum TEXT DEFAULT NULL,
  no_refresh TEXT DEFAULT 'false',
  last_queried_timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_refreshed_timestamp TIMESTAMP DEFAULT NULL,
  temp_set_value TEXT DEFAULT NULL
);
CREATE UNIQUE INDEX device_path ON data(device, path);

-- errors table
CREATE TABLE errors (
  device TEXT NOT NULL,
  message TEXT DEFAULT NULL,
  added_timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_errors_device ON errors(device);

EOF

mkdir -p /root/.zoom/logs

chmod 660 /dev/shm/microservice.db
chown root:www-data /dev/shm/microservice.db

if [ "$INTERACTIVE" == "true" ]
then
    /controller/app/OpenAVControllerApp
else    
    echo "> launching task processing"
    /usr/bin/nohup /usr/bin/php /var/www/html/api.php > /var/log/process_log &

    echo "> starting web server"
    apachectl -D FOREGROUND
fi