#include "OpenAVControllerApp.h"
#include <csignal>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <sqlite3.h>

#if defined(__linux) || defined(__linux__) || defined(linux)
#include "uv.h"
#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

OpenAVControllerApp app;

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
void inputCallback(HWND hwnd, UINT uMsg, UINT timerId, DWORD dwTime)
#elif defined(__linux) || defined(__linux__) || defined(linux)
void inputCallback(uv_timer_t *handle)
#endif
{
    const char* req = getInputLine();
    if (req) 
    {
        app.ReceiveCommand(req);
    }
}


#if defined(__linux) || defined(__linux__) || defined(linux)
void heartBeat(uv_timer_t *handle)
{
    app.HeartBeat() ;
}
#endif

std::string global_device = "unknown" ;

uv_timer_t clientReqTimer;
uv_timer_t heartBeatTimer;
void handle_sigint( int signum ) {
    std::cout << "\nCaught SIGINT, exiting..." << std::endl ;
    uv_timer_stop( &clientReqTimer ) ;
    uv_close( (uv_handle_t*)&clientReqTimer, nullptr ) ;
    uv_timer_stop( &heartBeatTimer ) ;
    uv_close( (uv_handle_t*)&heartBeatTimer, nullptr ) ;
    uv_stop( uv_default_loop() ) ;
    uv_loop_close( uv_default_loop() ) ;
    uv_walk( uv_default_loop(), [](uv_handle_t* handle, void*) {
        if (!uv_is_closing(handle)) {
            std::cout << "handle still open: " << uv_handle_type_name(handle->type) << std::endl;
        }
    }, nullptr);
    std::ofstream file("/dev/shm/" + global_device + ".fifo", std::ios::app);  // open in append mode
    if (file.is_open()) {
        file << "exit\n";
        file.close();
    }
    // std::exit( 0 ) ;
}


// ChatGPT
std::string get_state_datum( const std::string& path) {
    sqlite3* db = nullptr ;
    int rc = sqlite3_open( "/dev/shm/microservice.db", &db ) ;
    if( rc!=SQLITE_OK &&
        rc!=SQLITE_DONE &&
        rc!=SQLITE_ROW ) {
        std::cerr << "SQLite error (open): " << sqlite3_errmsg( db ) << ", on path: " << path << std::endl ;
        sqlite3_close( db ) ;
    }

    const char* select_sql =
        "SELECT datum FROM data WHERE device=:device AND"
        "                             path=:path LIMIT 1" ;
    sqlite3_stmt* select = nullptr ;
    rc = sqlite3_prepare_v2( db, select_sql, -1, &select, nullptr ) ;
    if( rc!=SQLITE_OK &&
        rc!=SQLITE_DONE &&
        rc!=SQLITE_ROW ) {
        std::cerr << "SQLite error (select prepare): " << sqlite3_errmsg( db ) << ", on path: " << path << std::endl ;
        sqlite3_close( db ) ;
    }

    sqlite3_bind_text( select, sqlite3_bind_parameter_index(select, ":device"), global_device.c_str(), -1, SQLITE_TRANSIENT ) ;
    sqlite3_bind_text( select, sqlite3_bind_parameter_index(select, ":path"), path.c_str(), -1, SQLITE_TRANSIENT ) ;

    std::string to_return = "" ;
    rc = sqlite3_step( select ) ;
    if( rc==SQLITE_ROW ) {
        const unsigned char* text = sqlite3_column_text(select, 0);
        to_return = text ? reinterpret_cast<const char*>(text) : "";
    }
    if( rc!=SQLITE_OK &&
        rc!=SQLITE_DONE &&
        rc!=SQLITE_ROW ) {
        std::cerr << "SQLite error (select loop done): " << sqlite3_errmsg( db ) << ", on path: " << path << std::endl ;
        sqlite3_close( db ) ;
    }
    sqlite3_finalize(select);
    sqlite3_close( db ) ;

    return to_return ;
}
    

int main(int argc, char *argv[])
{
    std::signal( SIGINT, handle_sigint ) ;

    if( argc>1 ) {
        global_device = argv[1] ;
    }

    pid_t pid = getpid() ;
    std::ofstream out( "/dev/shm/" + global_device + ".pid" ) ;
    if( out.is_open() ) {
        out << pid ;
        out.close() ;
    }

    std::cout << "> starting" << std::endl ;
    app.AppInit( global_device ) ;

    std::cout << "                    ___                      ___     __                    " << std::endl ;
    std::cout << "                   / _ \\ _ __   ___ _ __    / \\ \\   / /                    " << std::endl ;
    std::cout << "                  | | | | '_ \\ / _ \\ '_ \\  / _ \\ \\ / /                     " << std::endl ;
    std::cout << "                  | |_| | |_) |  __/ | | |/ ___ \\ V /                      " << std::endl ;
    std::cout << "                   \\___/| .__/ \\___|_| |_/_/   \\_\\_/                       " << std::endl ;
    std::cout << "  _____                 |_|    ____            _             _ _           " << std::endl ;
    std::cout << " |__  /___   ___  _ __ ___    / ___|___  _ __ | |_ _ __ ___ | | | ___ _ __ " << std::endl ;
    std::cout << "   / // _ \\ / _ \\| '_ ` _ \\  | |   / _ \\| '_ \\| __| '__/ _ \\| | |/ _ \\ '__|" << std::endl ;
    std::cout << "  / /| (_) | (_) | | | | | | | |__| (_) | | | | |_| | | (_) | | |  __/ |   " << std::endl ;
    std::cout << " /____\\___/ \\___/|_| |_| |_|  \\____\\___/|_| |_|\\__|_|  \\___/|_|_|\\___|_|   " << std::endl ;
                                                                               
    std::cout << "" << std::endl ;
    std::cout << "" << std::endl ;
    if( global_device!="unknown" ) {
        std::thread( []{ // background work

            std::cout << "> trying to re-pair" << std::endl ;
            bool retry_to_pair_result = false ;
            app.ReceiveCommand( "retry_to_pair" ) ;
            for( int i=0 ; i<50 ; i++ ) {
                std::cout << "." ;
                std::this_thread::sleep_for( std::chrono::milliseconds(100) ) ;
                std::string datum = get_state_datum( "paired" ) ;
                if( datum!="" ) {
                    if( datum=="true" ) {
                        retry_to_pair_result = true ;
                    }
                    break ;
                }
            }
            std::cout << std::endl ;
            if( retry_to_pair_result ) {
                std::cout << ">   success" << std::endl ;
            } else {
                std::cout << ">   no go" << std::endl ;
                std::string device_activation_code = "" ;
                auto colon = global_device.find( ":" ) ;
                if( colon!=std::string::npos ) {
                    auto at = global_device.find( "@", colon + 1 ) ;
                    if( !(at==std::string::npos || at==colon + 1) ) {
                        device_activation_code = global_device.substr( colon + 1, at - (colon + 1) ) ;
                    }

                }
                if( device_activation_code!="" ) {
                    std::cout << ">     pairing with activation code gotten as parameter" << std::endl ;
                    app.ReceiveCommand( "pair " + device_activation_code ) ;
                }
            }
        } ).detach() ;    
    }
    std::cout << "interactive mode, type \"help\" for available commands" << std::endl ;
                                                                            

#if defined(__linux) || defined(__linux__) || defined(linux)
    
    uv_timer_init(uv_default_loop(), &clientReqTimer);
    uv_timer_start(&clientReqTimer, inputCallback, 1000, 1000);

    uv_timer_init(uv_default_loop(), &heartBeatTimer);
    uv_timer_start(&heartBeatTimer, heartBeat, 150, 150);

    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

#endif

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

    SetTimer(NULL, 0, 1000 * 0.5, (TIMERPROC)&inputCallback);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
#endif

    return 0 ;
}
