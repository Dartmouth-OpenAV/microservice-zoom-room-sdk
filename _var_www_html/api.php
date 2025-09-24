<?php

// config
$refresh_data_every = 1 ; // minutes
$refresh_for_how_long = 5 ; // minutes
$maintenance_every = 10 ; // iteration
$zoom_room_command_retry_count = 2 ;


//  _____         _      ____                              _             
// |_   _|_ _ ___| | __ |  _ \ _ __ ___   ___ ___  ___ ___(_)_ __   __ _ 
//   | |/ _` / __| |/ / | |_) | '__/ _ \ / __/ _ \/ __/ __| | '_ \ / _` |
//   | | (_| \__ \   <  |  __/| | | (_) | (_|  __/\__ \__ \ | | | | (_| |
//   |_|\__,_|___/_|\_\ |_|   |_|  \___/ \___\___||___/___/_|_| |_|\__, |
//                                                                 |___/ 


function instantiate_process_for_device_if_needed( $device ) {
    $instantiate = false ;
    if( file_exists("/dev/shm/{$device}.pid") ) {
        // we make sure the process still exists
        if( !posix_kill(intval(trim(file_get_contents("/dev/shm/{$device}.pid"))), 0) ) {
            $instantiate = true ;
        } else {
            return true ;
        }
    } else {
        $instantiate = true ;
    }

    if( $instantiate ) {
        // in case either 1 did exist
        @unlink( "/dev/shm/{$device}.pid" ) ;
        @unlink( "/dev/shm/{$device}.fifo" ) ;
        @unlink( "/dev/shm/{$device}.queue" ) ;
        @unlink( "/dev/shm/{$device}.stdout" ) ;
        @unlink( "/dev/shm/{$device}.sterr" ) ;

        echo "instantiating controller app\n" ;
        shell_exec( "/fifo_runner.sh /controller/app/OpenAVControllerApp {$device} /dev/shm/{$device} > /dev/shm/{$device}.stdout 2>/dev/shm/{$device}.stderr &" ) ;
    }

    $sleep_counter = 0 ;
    while( $sleep_counter<30 &&
           !file_exists("/dev/shm/{$device}.pid") ) {
        usleep( 100000 ) ;
        $sleep_counter++ ;
    }
    if( !file_exists("/dev/shm/{$device}.pid") ) {
        add_error( $device, "unable to instantiate controller app" ) ;
        return false ;
    }
    if( !posix_kill(intval(trim(file_get_contents("/dev/shm/{$device}.pid"))), 0) ) {
        add_error( $device, "unable to instantiate controller app (pid file exists but process isn't up)" ) ;
        return false ;
    }
    // with this basic process stuff out of the way, we now wait for successful pairing with Zoom
    $sleep_counter = 0 ;
    while( $sleep_counter<5 &&
           sqlite_query("SELECT datum FROM data WHERE device=:device AND
                                                       path=:path", [':device'=>$device,
                                                                     ':path'=>"paired"], true)!="true" ) {
        sleep( 1 ) ;
        $sleep_counter++ ;
    }
    if( sqlite_query("SELECT datum FROM data WHERE device=:device AND
                                                   path=:path", [':device'=>$device,
                                                                 ':path'=>"paired"], true)!="true" ) {
        add_error( $device, "unable to pair controller with Zoom" ) ;
        return false ;
    }

    // if we made it here, we must be all set
    return true ;
}


if( is_cli() ) {  
    if( isset($argv[1]) ) {
        $id = $argv[1] ;
        $task = sqlite_query( "SELECT * FROM tasks WHERE id=:id", [":id"=>$id] ) ;
        if( count($task)==1 ) {
            $task = $task[0] ;

            $function_name = "" ;
            if( $task['method']=="GET" ) {
                $function_name .= "get_" ;
            } else if( $task['method']=="PUT" ) {
                $function_name .= "set_" ;
            } else if( $task['method']=="DELETE" ) {
                $function_name .= "unset_" ;
            }
            $function_name .= str_replace( "/", "_", $task['path'] ) ;

            if( function_exists($function_name) ) {
                $instantiation_result = instantiate_process_for_device_if_needed( $task['device'] ) ;
                if( $instantiation_result===true ) {
                    $result = call_user_func( $function_name, $task['device'], json_decode($task['datum'], true) ) ;
                    if( $task['method']=="GET" ) {
                        // datum isn't actually maintained here, controller app does that
                        // sqlite_query( "UPDATE data SET datum=:datum,
                        //                                last_refreshed_timestamp=CURRENT_TIMESTAMP WHERE device=:device AND
                        //                                                                                 path=:path", [':datum'=>json_encode($result),
                        //                                                                                               ':device'=>$task['device'],
                        //                                                                                               ':path'=>$task['path']] ) ;
                    }
                }
            } else {
                add_error( $task['device'], "function: {$function_name} not defined" ) ;
            }
            // we update last_refreshed_timestamp no matter what to avoid launching into the next refresh task immediately after
            sqlite_query( "UPDATE data SET last_refreshed_timestamp=CURRENT_TIMESTAMP WHERE device=:device AND
                                                                                            path=:path", [':device'=>$task['device'],
                                                                                                          ':path'=>$task['path']] ) ;
        } else {
            add_error( "all", "didn't find exactly 1 task for task id: {$id}" ) ;
        }
        sqlite_query( "DELETE FROM tasks WHERE id=:id", [':id'=>$id] ) ;
    } else {
        $maintenance_every_counter = $maintenance_every ;
        while( true ) {
            $devices_currently_in_process = sqlite_query( "SELECT DISTINCT(device) AS distinct_device FROM tasks WHERE in_process='true'", [] ) ;
            $devices_currently_in_process_for_query = [] ;
            foreach( $devices_currently_in_process as $device_currently_in_process ) {
                $devices_currently_in_process_for_query[] = $device_currently_in_process['distinct_device'] ;
            }
            $devices_currently_in_process_for_query = "'" . implode( "','", $devices_currently_in_process_for_query ) . "'" ;
            
            // first priority goes to write operations
            $query = "SELECT id FROM tasks WHERE " ;
            if( count($devices_currently_in_process)>0 ) {
                $query .= "device NOT IN ({$devices_currently_in_process_for_query}) AND " ;
            }
            $query .= "in_process='false' AND (method='PUT' OR method='PATCH' OR method='DELETE') AND process_at_timestamp<=CURRENT_TIMESTAMP ORDER BY added_timestamp, id ASC LIMIT 1" ;
            $task = sqlite_query( $query, [] ) ;

            // second goes to GETs
            if( count($task)==0 ) {
                $query = "SELECT id FROM tasks WHERE " ;
                if( count($devices_currently_in_process)>0 ) {
                    $query .= "device NOT IN ({$devices_currently_in_process_for_query}) AND " ;
                }
                $query .= "in_process='false' AND method='GET' AND process_at_timestamp<=CURRENT_TIMESTAMP ORDER BY added_timestamp, id ASC LIMIT 1" ;
                $task = sqlite_query( $query, [] ) ;
            }
            
            if( count($task)>0 ) {
                // run tasks if any
                $id = $task[0]['id'] ;
                sqlite_query( "UPDATE tasks SET in_process='true',
                                                in_process_since_timestamp=CURRENT_TIMESTAMP WHERE id=:id", [":id"=>$id] ) ;
                echo "> " . date( "Y-m-d H:i:s" ) . " - forking task {$id}, log @ /var/log/task.{$id}.log\n" ;
                shell_exec( "/usr/bin/timeout 9 php /var/www/html/api.php {$id} > /var/log/task.{$id}.log 2>&1 &" ) ;
            } else {
                $maintenance_every_counter = $maintenance_every_counter - 1 ;
                if( $maintenance_every_counter==0 ) {
                    $maintenance_every_counter = $maintenance_every ;
                    // maintenance and sleeping a little

                    // cancelling connection_stage of needs_passcode*
                    $devices_in_needs_passcode_connection_stage = sqlite_query( "SELECT DISTINCT(device) AS device FROM data WHERE path='meeting/connection_stage' AND
                                                                                                                                   datum LIKE 'needs_passcode%' AND
                                                                                                                                   last_refreshed_timestamp<DATETIME('now', '-5 minutes')", [] ) ;
                    foreach( $devices_in_needs_passcode_connection_stage as $device_in_needs_passcode_connection_stage ) {
                        echo "> " . date( "Y-m-d H:i:s" ) . " - device: {$device_in_needs_passcode_connection_stage['device']} has been waiting for passcode for more than 5 minutes, cancelling\n" ;
                        file_put_contents( "/dev/shm/{$device_in_needs_passcode_connection_stage['device']}.fifo", "cancel_entering_meeting_password\n" ) ;
                        sqlite_query( "UPDATE data SET last_refreshed_timestamp=CURRENT_TIMESTAMP WHERE device=:device AND
                                                                                                        path=:path", [':device'=>$device_in_needs_passcode_connection_stage['device'],
                                                                                                                      ':path'=>'meeting/connection_stage'] ) ;
                    }
                    
                    //   cleaning up obsolete data
                    // sqlite_query( "DELETE FROM data WHERE last_queried_timestamp<DATETIME('now', '-{$refresh_for_how_long} minutes')", [] ) ;
                    $devices_in_db = sqlite_query( "SELECT DISTINCT(device) AS device FROM data", [] ) ;
                    $devices_to_reap = [] ;
                    foreach( $devices_in_db as $device_in_db ) {
                        $recently_queried_count = sqlite_query( "SELECT COUNT(1) FROM data WHERE device=:device AND
                                                                                                 last_queried_timestamp>=DATETIME('now', '-{$refresh_for_how_long} minutes')", [':device'=>$device_in_db['device']], true ) ;
                        if( $recently_queried_count==0 ) {
                            $devices_to_reap[] = $device_in_db['device'] ;
                        }
                    }
                    if( count($devices_to_reap)>0 ) {
                        echo "> " . date( "Y-m-d H:i:s" ) . " - killing controller processes no longer tied to a device\n" ;
                        foreach( $devices_to_reap as $device_to_reap ) {
                            echo "> " . date( "Y-m-d H:i:s" ) . " -   {$device_to_reap}\n" ;
                            $pid_files = glob( "/dev/shm/*.pid", GLOB_NOSORT ) ;
                            foreach( $pid_files as $pid_file ) {
                                $device = substr( $pid_file, 9 ) ;
                                $device = substr( $device, 0, strlen($device)-4 ) ;
                                if( $device==$device_to_reap ) {
                                    $pid = intval( trim(file_get_contents($pid_file)) ) ;
                                    $ppid = get_parent_pid( $pid ) ;
                                    echo "> " . date( "Y-m-d H:i:s" ) . " -   killing pid: {$pid} & ppid: {$ppid} for device: {$device}\n" ;
                                    
                                    posix_kill( $pid, SIGINT ) ;
                                    // to release a blocking fgets
                                    file_put_contents( "/dev/shm/{$device}.fifo", "exit", FILE_APPEND ) ;
                                    posix_kill( $ppid, SIGTERM ) ;
                                    usleep( 100000 ) ; // 0.1 second
                                    @unlink( "/dev/shm/{$device}.pid" ) ;
                                    @unlink( "/dev/shm/{$device}.fifo" ) ;
                                    @unlink( "/dev/shm/{$device}.queue" ) ;
                                    @unlink( "/dev/shm/{$device}.stdout" ) ;
                                    @unlink( "/dev/shm/{$device}.stderr" ) ;
                                }
                            }
                            echo "> " . date( "Y-m-d H:i:s" ) . " -   purging db for for device: {$device_to_reap}\n" ;
                            sqlite_query( "DELETE FROM data WHERE device=:device", [':device'=>$device_to_reap] ) ;
                        }
                    }

                    $obsolete_tasks = sqlite_query( "SELECT * FROM tasks WHERE in_process='true' AND
                                                                               (in_process_since_timestamp<DATETIME('now', '-20 seconds') OR in_process_since_timestamp IS NULL)", [] ) ;
                    if( count($obsolete_tasks)>0 ) {
                        add_error( "all", "found obsolete tasks: " . json_encode($obsolete_tasks, JSON_PRETTY_PRINT) ) ;
                        sqlite_query( "DELETE FROM tasks WHERE in_process='true' AND
                                                               (`in_process_since_timestamp`<DATETIME('now', '-20 seconds') OR in_process_since_timestamp IS NULL)", [] ) ;
                    }
                    
                    //   adding tasks for data that is still being queried
                    $data_still_being_queried = sqlite_query( "SELECT * FROM data WHERE no_refresh='false' AND
                                                                                        (last_queried_timestamp>DATETIME('now', '-{$refresh_for_how_long} minutes') AND
                                                                                        (last_refreshed_timestamp<DATETIME('now', '-{$refresh_data_every} minutes') OR last_refreshed_timestamp IS NULL))", [] ) ;
                    foreach( $data_still_being_queried as $datum ) {
                        $in_tasks_count = sqlite_query( "SELECT COUNT(1) FROM tasks WHERE device=:device AND
                                                                                          path=:path AND
                                                                                          method='GET'", [':device'=>$datum['device'],
                                                                                                          ':path'=>$datum['path']], true ) ;
                        if( $in_tasks_count===0 ) {
                            echo "> " . date( "Y-m-d H:i:s" ) . " - adding task GET {$datum['device']}/{$datum['path']}\n" ;
                            sqlite_query( "INSERT INTO tasks (device,
                                                              path,
                                                              method) VALUES (:device,
                                                                              :path,
                                                                              'GET')", [':device'=>$datum['device'],
                                                                                        ':path'=>$datum['path']] ) ;
                        }
                    }

                    // 1% chance of gathering errors from *.stderr
                    if( mt_rand(0,99)==0 ) {
                        echo "> " . date( "Y-m-d H:i:s" ) . " - gathering errors from *.stderr\n" ;
                        $stderr_files = glob( "/dev/shm/*.stderr" ) ;
                        clearstatcache() ;
                        foreach( $stderr_files as $stderr_file ) {
                            echo "> " . date( "Y-m-d H:i:s" ) . " -   {$stderr_file}: " ;
                            if( filesize($stderr_file)>0 ) {
                                echo "has errors\n" ;
                                $device = str_replace( "/dev/shm/", "", $stderr_file ) ;
                                $device = str_replace( ".stderr", "", $device ) ;
                                add_error( $device, "stderr content: " . file_get_contents($stderr_file) ) ;
                                $fp = fopen( $stderr_file, "r+" ) ;
                                if( $fp ) {
                                    ftruncate( $fp, 0 ) ; // truncate to 0 bytes
                                    fclose( $fp ) ;
                                }
                            } else {
                                echo "ok\n" ;
                            }
                        }
                        shell_exec( '/usr/bin/find /var/log -name "task.*.log" -type f -mtime +10 -exec rm \{\} \\;' ) ;
                    }
                    
                    // 1% chance of removing task log files older than 10 days
                    if( mt_rand(0,99)==0 ) {
                        echo "> " . date( "Y-m-d H:i:s" ) . " - removing task log files older than 10 days\n" ;
                        shell_exec( '/usr/bin/find /var/log -name "task.*.log" -type f -mtime +10 -exec rm \{\} \\;' ) ;
                    }
                }
                usleep( 100000 ) ; // microseconds
            }
        }
    }
    exit( 0 ) ;
}




//  ____            __ _ _       _     _   
// |  _ \ _ __ ___ / _| (_) __ _| |__ | |_ 
// | |_) | '__/ _ \ |_| | |/ _` | '_ \| __|
// |  __/| | |  __/  _| | | (_| | | | | |_ 
// |_|   |_|  \___|_| |_|_|\__, |_| |_|\__|
//                         |___/           


header( "Access-Control-Allow-Origin: *" ) ;
header( "Access-Control-Allow-Credentials: true" ) ;
header( "Access-Control-Allow-Methods: GET,HEAD,OPTIONS,POST,PUT,PATCH,DELETE,EXPORT" ) ;
header( "Access-Control-Allow-Headers: Access-Control-Allow-Headers, Origin,Accept, X-Requested-With, Content-Type, Access-Control-Request-Method, Access-Control-Request-Headers, Authorization" ) ;

if( $_SERVER['REQUEST_METHOD']=="OPTIONS" ) {
    http_response_code( 200 ) ;
    exit( 0 ) ;
}




//  ____             _   _             
// |  _ \ ___  _   _| |_(_)_ __   __ _ 
// | |_) / _ \| | | | __| | '_ \ / _` |
// |  _ < (_) | |_| | |_| | | | | (_| |
// |_| \_\___/ \__,_|\__|_|_| |_|\__, |
//                               |___/ 

$method = $_SERVER['REQUEST_METHOD'] ;
$request_uri = $_SERVER['REQUEST_URI'] ;
$request_uri = explode( "/", $request_uri ) ;
$device = $request_uri[1] ;
$activation_code_holder = substr( explode("@", $device )[0], 1 ) ;
if( !preg_match('/^\d+-\d+-\d+\d+$/', $activation_code_holder) &&
    str_starts_with(base64_decode($activation_code_holder), "http") ) {
    $ch = curl_init() ;
    $url = base64_decode( $activation_code_holder ) ;
    $activation_code = null ;
    $activation_code_filename = "/dev/shm/activation_code_" . md5( $url ) ;
    if( file_exists($activation_code_filename) ) {
        $activation_code = file_get_contents( $activation_code_filename ) ;
    } else {
        curl_setopt( $ch, CURLOPT_URL, $url ) ;
        curl_setopt( $ch, CURLOPT_SSL_VERIFYPEER, false ) ;
        curl_setopt( $ch, CURLOPT_CUSTOMREQUEST, "GET" ) ;
        curl_setopt( $ch, CURLOPT_CONNECTTIMEOUT, 1 ) ;
        curl_setopt( $ch, CURLOPT_RETURNTRANSFER, true ) ;
        curl_setopt( $ch, CURLOPT_TIMEOUT, 2 ) ;
        $response = curl_exec( $ch ) ;
        $response_code = curl_getinfo( $ch, CURLINFO_HTTP_CODE ) ;
        $curl_errno = curl_errno( $ch ) ;
        curl_close( $ch ) ;
        if( $response_code==200 ) {
            if( $response[0]=='"' && $response[strlen($response)-1]=='"' ) {
                $response = json_decode( $response ) ;
            }
            $activation_code = $response ;
        }
    }
    if( $activation_code===null ) {
        close_with_400( "unable to retrieve activation code" ) ;
        exit( 1 ) ;
    }

    // we're good!
    file_put_contents( $activation_code_filename, $activation_code ) ;
    $device = ":{$activation_code}@" . explode( "@", $device )[1] ;
}
$path = implode( "/", array_slice($request_uri, 2) ) ;
$path = explode( "?", $path ) ;
$path = $path[0] ;

if( $path=="meeting/presence/sharing/camera" &&
    $method=="GET" ) {
    get() ;
}
if( $path=="meeting/presence/sharing/camera" &&
    $method=="PUT" ) {
    set() ;
}

if( $path=="meeting/presence/microphone_muted" &&
    $method=="GET" ) {
    get() ;
}
if( $path=="meeting/presence/microphone_muted" &&
    $method=="PUT" ) {
    set() ;
}

if( $path=="meeting/presence/video_muted" &&
    $method=="GET" ) {
    get() ;
}
if( $path=="meeting/presence/video_muted" &&
    $method=="PUT" ) {
    set() ;
}

if( $path=="meeting/status" &&
    $method=="GET" ) {
    get_hierarchy() ;
}

if( $path=="meeting/info" &&
    $method=="GET" ) {
    get_hierarchy() ;
}

if( $path=="meeting" &&
    $method=="GET" ) {
    get_hierarchy() ;
}
if( $path=="meeting" &&
    $method=="PUT" ) {
    set() ;
}
if( $path=="meeting" &&
    $method=="DELETE" ) {
    delete() ;
}
if( $path=="meeting_list" &&
    $method=="GET" ) {
    get() ;
}
if( $path=="sharing_key" &&
    $method=="GET" ) {
    get() ;
}
if( $path=="pairing_code" &&
    $method=="GET" ) {
    get() ;
}
if( $path=="errors" &&
    $method=="GET" ) {
    get_errors() ;
}

close_with_400( "unknown path" ) ;
exit( 1 ) ;




//  _   _ _____ _____ ____     ____      _      ______       _   
// | | | |_   _|_   _|  _ \   / ___| ___| |_   / / ___|  ___| |_ 
// | |_| | | |   | | | |_) | | |  _ / _ \ __| / /\___ \ / _ \ __|
// |  _  | | |   | | |  __/  | |_| |  __/ |_ / /  ___) |  __/ |_ 
// |_| |_| |_|   |_| |_|      \____|\___|\__/_/  |____/ \___|\__|
                                                                                                                                        

function get() {
    global $device, $path, $method ;

    $entry_count = sqlite_query( "SELECT COUNT(1) FROM data WHERE device=:device AND
                                                                  path=:path", [':device'=>$device,
                                                                                ':path'=>$path], true ) ;
    if( $entry_count==0 ) {
        sqlite_query( "INSERT INTO data (device,
                                         path,
                                         datum) VALUES (:device,
                                                        :path,
                                                        null)", [':device'=>$device,
                                                                 ':path'=>$path] ) ;
        close_with_204() ;
    }
    if( $entry_count>0 ) {
        if( $entry_count>1 ) {
            // found bug with very quick initial queries to same path/device, workaround until better solution
            sqlite_query( "DELETE FROM data WHERE device=:device AND
                                                  path=:path LIMIT " . ($entry_count-1), [':device'=>$device,
                                                                                          ':path'=>$path] ) ;
            // add_error( $device, "more than 1 entry count for {$method} {$path} with: {$entry_count}" ) ;
        }
        $datum = sqlite_query( "SELECT datum FROM data WHERE device=:device AND
                                                             path=:path LIMIT 1", [':device'=>$device,
                                                                                   ':path'=>$path], true ) ;
        sqlite_query( "UPDATE data SET last_queried_timestamp=CURRENT_TIMESTAMP WHERE device=:device AND
                                                                                      path=:path", [':device'=>$device,
                                                                                                    ':path'=>$path] ) ;

        

        if( $datum===null || $datum==="" ) {
            close_with_204() ;
        } else {
            close_with_200( json_decode($datum, true) ) ;
        }
    }

    close_with_500( "we shouldn't reach this point" ) ;
}


function get_hierarchy() {
    global $device, $path, $method ;

    $entry_count = sqlite_query( "SELECT COUNT(1) FROM data WHERE device=:device AND
                                                                  (path LIKE :path1 OR path=:path2)", [':device'=>$device,
                                                                                                       ':path1'=>"{$path}/%",
                                                                                                       ':path2'=>$path], true ) ;

    if( $entry_count==0 ) {
        // add a datum so device gets a process tied to it
        sqlite_query( "INSERT INTO data (device,
                                         path,
                                         datum) VALUES (:device,
                                                        :path,
                                                        null)", [':device'=>$device,
                                                                 ':path'=>$path] ) ;
        close_with_204() ;
    }
    if( $entry_count>0 ) {
        $data = sqlite_query( "SELECT path,
                                      datum FROM data WHERE device=:device AND
                                                            (path LIKE :path1 OR path=:path2)", [':device'=>$device,
                                                                                                 ':path1'=>"{$path}/%",
                                                                                                 ':path2'=>$path] ) ;
        sqlite_query( "UPDATE data SET last_queried_timestamp=CURRENT_TIMESTAMP WHERE device=:device AND
                                                                                      (path LIKE :path1 OR path=:path2)", [':device'=>$device,
                                                                                                                           ':path1'=>"{$path}/%",
                                                                                                                           ':path2'=>$path] ) ;

        

        $data = get_path_hierarchy( $data ) ;

        $path = explode( "/", $path ) ;
        while( count($path)>0 ) {
            $dir = array_shift( $path ) ;
            $data = $data[$dir] ;
        }
        
        if( $data===null || $data==="" ) {
            close_with_204() ;
        } else {
            close_with_200( $data, true ) ;
        }
    }

    close_with_500( "we shouldn't reach this point" ) ;
}


function set( $no_refresh=false ) {
    global $device, $path, $method ;

    $datum = get_request_body() ;

    if( $no_refresh===true ) {
        $no_refresh = "true" ;
    } else {
        $no_refresh = "false" ;
    }

    sqlite_query( "DELETE FROM tasks WHERE device=:device AND
                                           path=:path AND
                                           method=:method", [':device'=>$device,
                                                             ':path'=>$path,
                                                             ':method'=>$method] ) ;
    if( $no_refresh==="false" ) {
        sqlite_query( "INSERT INTO tasks (device,
                                          path,
                                          method,
                                          datum) VALUES (:device,
                                                         :path,
                                                         :method,
                                                         :datum)", [':device'=>$device,
                                                                    ':path'=>$path,
                                                                    ':method'=>$method,
                                                                    ':datum'=>$datum] ) ;

    }

    close_with_200( "ok" ) ;
}


function set_with_delay() {
    global $device, $path, $method ;

    $data = get_request_body() ;
    $parsed_data = json_decode( $data, true ) ;
    $delay = 0 ;
    if( !isset($parsed_data['delay']) ) {
        add_error( $device, "need delay variable when calling set_with_delay" ) ;
    } else {
        $delay = $parsed_data['delay'] ;
    }

    sqlite_query( "INSERT INTO tasks (device,
                                      path,
                                      method,
                                      datum,
                                      process_at_timestamp) VALUES (:device,
                                                                    :path,
                                                                    :method,
                                                                    datetime('now', '+{$delay} seconds'))", [':device'=>$device,
                                                                                                             ':path'=>$path,
                                                                                                             ':method'=>$method] ) ;

    close_with_200( "ok" ) ;
}


function delete() {
    global $device, $path, $method ;

    sqlite_query( "INSERT INTO tasks (device,
                                      path,
                                      method,
                                      datum) VALUES (:device,
                                                     :path,
                                                     :method,
                                                     :datum)", [':device'=>$device,
                                                                ':path'=>$path,
                                                                ':method'=>$method,
                                                                ':datum'=>$datum] ) ;

    close_with_200( "ok" ) ;
}




//  ____                             _          _    ____      _      ______       _   
// |  _ \  ___  ___ ___  _   _ _ __ | | ___  __| |  / ___| ___| |_   / / ___|  ___| |_ 
// | | | |/ _ \/ __/ _ \| | | | '_ \| |/ _ \/ _` | | |  _ / _ \ __| / /\___ \ / _ \ __|
// | |_| |  __/ (_| (_) | |_| | |_) | |  __/ (_| | | |_| |  __/ |_ / /  ___) |  __/ |_ 
// |____/ \___|\___\___/ \__,_| .__/|_|\___|\__,_|  \____|\___|\__/_/  |____/ \___|\__|
//                            |_|                                                                                                           |___/                                      


function get_meeting( $device ) {
    // this state doesn't come from a device, it's automatically maintained asyncronously by the controller app
    return null ;
}


function set_meeting( $device, $data ) {
    if( !is_array($data) ||
        !array_key_exists('meeting_id', $data) ||
        !array_key_exists('meeting_passcode', $data) ) {
        add_error( $device, "erroneous data provided to join_meeting with: " . json_encode($data) ) ;
    } else {
        // maybe we're already in a meeting?
        if( sqlite_query("SELECT datum FROM data WHERE device=:device AND
                                                       path=:path", [$device,
                                                                      "meeting/status"], true)=="in_meeting" ) {
            echo "already in meeting\n" ;
            sqlite_query( "INSERT INTO data (device,
                                             path,
                                             datum,
                                             no_refresh) VALUES (:device,
                                                                 :path,
                                                                 :datum,
                                                                 'true')
                                  ON CONFLICT (device,
                                               path) DO UPDATE SET datum=:datum", [':device'=>$device,
                                                                                   ':path'=>"meeting/last_join_result",
                                                                                   ':datum'=>"failure - already in meeting"] ) ;
        } else {
            $meeting_connection_stage = sqlite_query( "SELECT datum FROM data WHERE device=:device AND
                                                                                    path=:path", [':device'=>$device,
                                                                                                ':path'=>"meeting/connection_stage"], true ) ;
            if( $meeting_connection_stage=="needs_passcode" ) {
                echo "looks like we are waiting on a passcode from a previous operation, cancelling\n" ;
                file_put_contents( "/dev/shm/{$device}.fifo", "cancel_entering_meeting_password\n" ) ;
                sleep( 2 ) ;
            }

            echo "join_meeting\n" ;
            file_put_contents( "/dev/shm/{$device}.fifo", "join_meeting {$data['meeting_id']}\n" ) ;

            // do we need to send a passcode?
            $meeting_status = null ;
            $passcode_has_been_sent = false ;
            $safety_counter = 50 ; // sleeps are 0.2s, so that's 10s total
            while( $meeting_status!="in_meeting" ) {
                usleep( 200000 ) ;
                echo "." ;
                $safety_counter-- ;
                if( $safety_counter<=0 ) {
                    echo "safety counter reached\n" ;
                } else {
                    $meeting_status = sqlite_query( "SELECT datum FROM data WHERE device=:device AND
                                                                                  path=:path", [':device'=>$device,
                                                                                                ':path'=>"meeting/status"], true ) ;
                    echo "meeting_status: {$meeting_status}\n" ;
                    if( $meeting_status=="connecting" ) {
                        $connection_stage = sqlite_query( "SELECT datum FROM data WHERE device=:device AND
                                                                                        path=:path", [':device'=>$device,
                                                                                                      ':path'=>"meeting/connection_stage"], true ) ;
                        echo "meeting_connection_stage: {$connection_stage}\n" ;
                        if( substr_count($connection_stage, "needs_passcode")>0 &&
                            $data['meeting_passcode']!="" &&
                            !$passcode_has_been_sent ) {
                            // now is the time to send in the passcode
                            file_put_contents( "/dev/shm/{$device}.fifo", "send_meeting_passcode {$data['meeting_passcode']}\n" ) ;
                            $passcode_has_been_sent = true ;
                        }
                    } else if( $meeting_status=="in_meeting" ) {
                        // all right!
                        echo "all set\n" ;
                        sqlite_query( "INSERT INTO data (device,
                                                   path,
                                                   datum,
                                                   no_refresh) VALUES (:device,
                                                                       :path,
                                                                       :datum,
                                                                       'true')
                                               ON CONFLICT (device,
                                                            path) DO UPDATE SET datum=:datum", [':device'=>$device,
                                                                                                ':path'=>"meeting/last_join_result",
                                                                                                ':datum'=>"success"] ) ;
                        break ;
                    }
                }
            }

            if( $meeting_status!="in_meeting" ) {
                echo "didn't reach in_meeting\n" ;
                sqlite_query( "INSERT INTO data (device,
                                                   path,
                                                   datum,
                                                   no_refresh) VALUES (:device,
                                                                       :path,
                                                                       :datum,
                                                                       'true')
                                      ON CONFLICT (device,
                                                   path) DO UPDATE SET datum=:datum", [':device'=>$device,
                                                                                       ':path'=>"meeting/last_join_result",
                                                                                       ':datum'=>"failure"] ) ;
            }
        }
    }
}


function unset_meeting( $device, $data ) {
    file_put_contents( "/dev/shm/{$device}.fifo", "leave_meeting\n" ) ;
}

function get_meeting_presence_sharing_camera( $device ) {
    // this state doesn't come from a device, it's automatically maintained asyncronously by the controller app
    return null ;
}

function set_meeting_presence_sharing_camera( $device, $data ) {
    $in_meeting = sqlite_query( "SELECT datum FROM data WHERE device=:device AND
                                                              path=:path", [':device'=>$device,
                                                                            ':path'=>"meeting"], true ) ;
    if( $in_meeting===null ) {
        file_put_contents( "/dev/shm/{$device}.fifo", "start_instant_meeting\n" ) ;
        $sleep_counter = 0 ;
        while( $sleep_counter<5 &&
               sqlite_query("SELECT datum FROM data WHERE device=:device AND
                                                           path=:path", [':device'=>$device,
                                                                         ':path'=>"meeting"], true)===null ) {
            sleep( 1 ) ;
            $sleep_counter++ ;
        }
    }
    file_put_contents( "/dev/shm/{$device}.fifo", "share_camera {$data}\n" ) ;
}


function get_meeting_muted( $device ) {
    // this state doesn't come from a device, it's automatically maintained asyncronously by the controller app
    return null ;
}

function get_meeting_list( $device ) {
    // this state doesn't come from a device, it's automatically maintained asyncronously by the controller app
    return null ;
}

function set_meeting_muted( $device, $data ) {
    if( $data=="true" || $data===true ) {
        file_put_contents( "/dev/shm/{$device}.fifo", "mute\n" ) ;
    } else if( $data=="false" || $data===false ) {
        file_put_contents( "/dev/shm/{$device}.fifo", "unmute\n" ) ;
    }
}


function get_meeting_video_muted( $device ) {
    // this state doesn't come from a device, it's automatically maintained asyncronously by the controller app
    return null ;
}

function set_meeting_video_muted( $device, $data ) {
    if( $data == "true" || $data === true ) {
        file_put_contents( "/dev/shm/{$device}.fifo", "mute_video\n" ) ;
    } else if ($data == "false" || $data === false) {
        file_put_contents( "/dev/shm/{$device}.fifo", "unmute_video\n" ) ;
    }
}


function add_error($device, $message)
{
    sqlite_query(
        "INSERT INTO errors (device,
                             message) VALUES (:device,
                                              :message)", [':device'=>$device,
                                                           ':message'=>$message] ) ;
}


function get_errors() {
    global $device ;

    $errors = sqlite_query( "SELECT message FROM errors WHERE device=:device", [':device'=>$device] ) ;
    sqlite_query( "DELETE FROM errors WHERE device=:device", [':device'=>$device] ) ;

    if( count($errors)>0 ) {
        close_with_500( $errors ) ;
    } else {
        close_with_200( "no errors" ) ;
    }
}



// TODO get errors




//  ____                               _     _____                 _   _                 
// / ___| _   _ _ __  _ __   ___  _ __| |_  |  ___|   _ _ __   ___| |_(_) ___  _ __  ___ 
// \___ \| | | | '_ \| '_ \ / _ \| '__| __| | |_ | | | | '_ \ / __| __| |/ _ \| '_ \/ __|
//  ___) | |_| | |_) | |_) | (_) | |  | |_  |  _|| |_| | | | | (__| |_| | (_) | | | \__ \
// |____/ \__,_| .__/| .__/ \___/|_|   \__| |_|   \__,_|_| |_|\___|\__|_|\___/|_| |_|___/
//             |_|   |_|                                                                 


function close_with_500( $message ) {
    global $path ;

    http_response_code( 500 ) ;

    header( "Content-Type: application/json" ) ;

    $to_return = array( "success"=>false, "message"=>$message ) ;
    echo json_encode( $to_return ) ;

    exit( 1 ) ;
}


function close_with_501( $message ) {
    global $path ;

    http_response_code( 501 ) ;

    header( "Content-Type: application/json" ) ;

    $to_return = array( "success"=>false, "message"=>$message ) ;
    echo json_encode( $to_return ) ;

    exit( 1 ) ;
}


function close_with_404( $message ) {
    global $path ;

    http_response_code( 404 ) ;

    header( "Content-Type: application/json" ) ;

    $to_return = array( "success"=>false, "message"=>$message ) ;
    echo json_encode( $to_return ) ;

    exit( 1 ) ;
}


function close_with_400( $message ) {
    global $path ;

    http_response_code( 400 ) ;

    header( "Content-Type: application/json" ) ;

    $to_return = array( "success"=>false, "message"=>$message ) ;
    echo json_encode( $to_return ) ;

    exit( 1 ) ;
}


function close_with_401( $message ) {
    global $path ;

    http_response_code( 401 ) ;

    echo "Unauthorized: {$message}" ;

    exit( 1 ) ;
}


function close_with_204() {
    global $path ;

    http_response_code( 204 ) ;

    exit( 1 ) ;
}


function close_with_200( $data ) {
    global $path ;

    header( "Content-Type: application/json; charset=utf-8" ) ;

    echo json_encode( $data ) ;

    exit( 0 ) ;
}


function get_request_body() {
    $input = file_get_contents( "php://input" ) ;
    // $input = json_decode( $input, true ) ;
    return $input ;
}


function is_cli() {
    return php_sapi_name()==="cli" ;
}


function get_device_info( $device ) {
    $device_original = $device ;
    $device = explode( "@", $device ) ;
    if( count($device)==1 ) {
        return array( 'username'=>"",
                      'password'=>"",
                      'fqdn'=>$device[0] ) ;
    } else if( count($device)==2 ) {
        $credentials = explode( ":", $device[0] ) ;
        if( count($credentials)==2 ) {
            return array( 'username'=>$credentials[0],
                          'password'=>urldecode($credentials[1]),
                          'fqdn'=>$device[1] ) ;
        } else {
            add_error( "all", "unparseable device credentials: {$device_original}" ) ;
        }
    } else {
        add_error( "all", "unparseable device: {$device_original}" ) ;
    }

    add_error( "all", "unreachable point #qeitrw7oe" ) ;
    return false ;
}


function sqlite_query( $query, $params=[], $one_result=false ) {
    $db = new PDO( "sqlite:/dev/shm/microservice.db" ) ;
    if( is_cli() ) {
        $db->setAttribute( PDO::ATTR_ERRMODE, PDO::ERRMODE_WARNING ) ;
    }

    $query_statement = $db->prepare( $query ) ;
    $ok = $query_statement->execute( $params ) ;

    if( !$ok && is_cli() ) {
        [$sql_state, $driver_code, $message] = $query_statement->errorInfo() ;
        echo "SQLite error: ($sql_state/$driver_code): $message\nQuery: {$query}\n\n" ;
    }

    $result = $query_statement->fetchAll( PDO::FETCH_ASSOC ) ;
    if( $result===null ) {
        return $result ;
    } else if( $one_result ) {
        if( count($result)==0 ) {
            return null ;
        }
        return $result[0][array_keys($result[0])[0]] ;
    } else {
        return $result ;
    }
}


// AI
function get_parent_pid(int $pid): ?int {
    $statusFile = "/proc/$pid/status";
    if (!file_exists($statusFile) || !is_readable($statusFile)) {
        return null;
    }

    foreach (file($statusFile) as $line) {
        if (strpos($line, 'PPid:') === 0) {
            return (int)trim(substr($line, 5));
        }
    }

    return null;
}


// AI
function get_path_hierarchy(array $items): array {
    $result = [];

    foreach ($items as $item) {
        $keys = explode('/', $item['path']);
        $value = $item['datum'];

        // Start at the top level
        $ref = &$result;

        // Traverse keys except the last one
        foreach (array_slice($keys, 0, -1) as $key) {
            if (!isset($ref[$key]) || !is_array($ref[$key])) {
                $ref[$key] = [];
            }
            $ref = &$ref[$key];
        }

        // Set final value
        $ref[end($keys)] = $value;
    }

    return $result;
}


?>