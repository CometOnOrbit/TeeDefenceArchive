/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_CONFIG_VARIABLES_H
#define ENGINE_SHARED_CONFIG_VARIABLES_H
#undef ENGINE_SHARED_CONFIG_VARIABLES_H // this file will be included several times

// TODO: remove this
#include "././game/variables.h"

MACRO_CONFIG_STR(Password, password, 32, "", CFGFLAG_SAVE | CFGFLAG_CLIENT | CFGFLAG_SERVER, "Password to the server")
MACRO_CONFIG_STR(Logfile, logfile, 128, "", CFGFLAG_SAVE | CFGFLAG_CLIENT | CFGFLAG_SERVER, "Filename to log all output to")
MACRO_CONFIG_INT(LogfileTimestamp, logfile_timestamp, 0, 0, 1, CFGFLAG_SAVE | CFGFLAG_CLIENT | CFGFLAG_SERVER, "Add a time stamp to the log file's name")
MACRO_CONFIG_INT(ConsoleOutputLevel, console_output_level, 0, 0, 2, CFGFLAG_SAVE | CFGFLAG_CLIENT | CFGFLAG_SERVER, "Adjusts the amount of information in the console")
MACRO_CONFIG_INT(ShowConsoleWindow, show_console_window, 1, 0, 3, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Show console window (0 = never, 1 = debug, 2 = release, 3 = always")

MACRO_CONFIG_STR(SvName, sv_name, 128, "unnamed server", CFGFLAG_SAVE | CFGFLAG_SERVER, "Server name")
MACRO_CONFIG_STR(SvHostname, sv_hostname, 128, "", CFGFLAG_SAVE | CFGFLAG_SERVER, "Server hostname")
MACRO_CONFIG_STR(Bindaddr, bindaddr, 128, "", CFGFLAG_SAVE | CFGFLAG_CLIENT | CFGFLAG_SERVER | CFGFLAG_MASTER, "Address to bind the client/server to")
MACRO_CONFIG_INT(SvPort, sv_port, 8303, 0, 0, CFGFLAG_SAVE | CFGFLAG_SERVER, "Port to use for the server")
MACRO_CONFIG_STR(SvMap, sv_map, 128, "dm1", CFGFLAG_SAVE | CFGFLAG_SERVER, "Map to use on the server")
MACRO_CONFIG_INT(SvMaxClients, sv_max_clients, 24, 1, MAX_CLIENTS, CFGFLAG_SAVE | CFGFLAG_SERVER, "Maximum number of clients that are allowed on a server")
MACRO_CONFIG_INT(SvMaxClientsPerIP, sv_max_clients_per_ip, 4, 1, MAX_CLIENTS, CFGFLAG_SAVE | CFGFLAG_SERVER, "Maximum number of clients with the same IP that can connect to the server")
MACRO_CONFIG_INT(SvMapDownloadSpeed, sv_map_download_speed, 8, 1, 16, CFGFLAG_SAVE | CFGFLAG_SERVER, "Number of map data packages a client gets on each request")
MACRO_CONFIG_INT(SvHighBandwidth, sv_high_bandwidth, 0, 0, 1, CFGFLAG_SAVE | CFGFLAG_SERVER, "Use high bandwidth mode. Doubles the bandwidth required for the server. LAN use only")
MACRO_CONFIG_INT(SvRegister, sv_register, 1, 0, 1, CFGFLAG_SAVE | CFGFLAG_SERVER, "Register server with master server for public listing")
MACRO_CONFIG_STR(SvRegisterCommunityToken, sv_register_community_token, 128, "", CFGFLAG_SERVER, "Token to register this server to a particular community")
MACRO_CONFIG_INT(SvFlag, sv_flag, -1, -1, 999, CFGFLAG_SERVER, "Country flag to group this community under (ISO 3166-1 numeric)")
MACRO_CONFIG_STR(SvRegisterUrl, sv_register_url, 128, "https://register1.ddnet.org/ddnet/15/register", CFGFLAG_SERVER, "Masterserver URL to register to")
MACRO_CONFIG_STR(SvRconPassword, sv_rcon_password, 32, "", CFGFLAG_SAVE | CFGFLAG_SERVER, "Remote console password (full access)")
MACRO_CONFIG_STR(SvRconModPassword, sv_rcon_mod_password, 32, "", CFGFLAG_SAVE | CFGFLAG_SERVER, "Remote console password for moderators (limited access)")
MACRO_CONFIG_INT(SvRconMaxTries, sv_rcon_max_tries, 3, 0, 100, CFGFLAG_SAVE | CFGFLAG_SERVER, "Maximum number of tries for remote console authentication")
MACRO_CONFIG_INT(SvRconBantime, sv_rcon_bantime, 5, 0, 1440, CFGFLAG_SAVE | CFGFLAG_SERVER, "The time a client gets banned if remote console authentication fails. 0 makes it just use kick")
MACRO_CONFIG_INT(SvAutoDemoRecord, sv_auto_demo_record, 0, 0, 1, CFGFLAG_SAVE | CFGFLAG_SERVER, "Automatically record demos")
MACRO_CONFIG_INT(SvAutoDemoMax, sv_auto_demo_max, 10, 0, 1000, CFGFLAG_SAVE | CFGFLAG_SERVER, "Maximum number of automatically recorded demos (0 = no limit)")
MACRO_CONFIG_STR(SvMaplist, sv_maplist, 32, "all", CFGFLAG_SAVE | CFGFLAG_SERVER, "Maplist for authed clients (none, standard, all)")

MACRO_CONFIG_STR(EcBindaddr, ec_bindaddr, 128, "localhost", CFGFLAG_SAVE | CFGFLAG_ECON, "Address to bind the external console to. Anything but 'localhost' is dangerous")
MACRO_CONFIG_INT(EcPort, ec_port, 0, 0, 0, CFGFLAG_SAVE | CFGFLAG_ECON, "Port to use for the external console")
MACRO_CONFIG_STR(EcPassword, ec_password, 32, "", CFGFLAG_SAVE | CFGFLAG_ECON, "External console password")
MACRO_CONFIG_INT(EcBantime, ec_bantime, 0, 0, 1440, CFGFLAG_SAVE | CFGFLAG_ECON, "The time a client gets banned if econ authentication fails. 0 just closes the connection")
MACRO_CONFIG_INT(EcAuthTimeout, ec_auth_timeout, 30, 1, 120, CFGFLAG_SAVE | CFGFLAG_ECON, "Time in seconds before the the econ authentification times out")
MACRO_CONFIG_INT(EcOutputLevel, ec_output_level, 1, 0, 2, CFGFLAG_SAVE | CFGFLAG_ECON, "Adjusts the amount of information in the external console")

MACRO_CONFIG_INT(NetTcpAbortOnClose, net_tcp_abort_on_close, 0, 0, 1, CFGFLAG_SAVE | CFGFLAG_SERVER | CFGFLAG_ECON, "Aborts tcp connection on close")

MACRO_CONFIG_INT(Debug, debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SERVER, "Debug mode")
MACRO_CONFIG_INT(DbgPref, dbg_pref, 0, 0, 1, CFGFLAG_SERVER, "Performance outputs")
MACRO_CONFIG_INT(DbgGraphs, dbg_graphs, 0, 0, 1, CFGFLAG_CLIENT, "Performance graphs")
MACRO_CONFIG_INT(DbgHitch, dbg_hitch, 0, 0, 0, CFGFLAG_SERVER, "Hitch warnings")
MACRO_CONFIG_INT(DbgResizable, dbg_resizable, 0, 0, 0, CFGFLAG_CLIENT, "Enables window resizing")
#ifdef CONF_DEBUG
MACRO_CONFIG_INT(DbgStress, dbg_stress, 0, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SERVER, "Stress systems")
MACRO_CONFIG_INT(DbgStressNetwork, dbg_stress_network, 0, 0, 0, CFGFLAG_CLIENT | CFGFLAG_SERVER, "Stress network")
MACRO_CONFIG_STR(DbgStressServer, dbg_stress_server, 32, "localhost", CFGFLAG_CLIENT, "Server to stress")
#endif

// TeeDefense
MACRO_CONFIG_INT(SvMaxTowerHealth, sv_max_tower_health, 100, 1, 1000, CFGFLAG_SERVER, "Main tower max health")
MACRO_CONFIG_INT(SvPlayerMaxHealth, sv_player_max_health, 10, 1, 1000, CFGFLAG_SERVER, "Maximum tee health")
MACRO_CONFIG_INT(SvZombWarmup, sv_zomb_warmup, 10, 0, 120, CFGFLAG_SERVER, "Seconds of warmup between waves")
MACRO_CONFIG_INT(SvMaxZombieSpawn, sv_max_zombie_spawn, 20, 1, 100, CFGFLAG_SERVER, "Max zombie slot spawn attempts per tick (anti-spike)")
MACRO_CONFIG_INT(SvTdZombieFirstSlot, sv_td_zombie_first_slot, 16, 1, MAX_CLIENTS - 1, CFGFLAG_SERVER, "First client slot reserved for zombie dummies")
MACRO_CONFIG_INT(SvTdTowerHitRadius, sv_td_tower_hit_radius, 200, 48, 360, CFGFLAG_SERVER, "Radius (px) for projectile/hammer hits against the main tower")
MACRO_CONFIG_INT(SvTdTowerTouchDamage, sv_td_tower_touch_damage, 1, 0, 50, CFGFLAG_SERVER, "Tower HP lost when a zombie reaches the tower (suicide rush)")
MACRO_CONFIG_INT(SvTdWaveScoreBonus, sv_td_wave_score_bonus, 1, 0, 1, CFGFLAG_SERVER, "Grant score to defenders when a wave is cleared")

MACRO_CONFIG_INT(SvTurretRadius, sv_turret_radius, 32, 8, 256, CFGFLAG_SERVER, "Turret ring base radius in pixels (visual grows per tier)")
MACRO_CONFIG_INT(SvTurretFireRange, sv_turret_fire_range, 800, 64, 4000, CFGFLAG_SERVER, "Turret auto-aim range vs dummy zombies (pixels)")
MACRO_CONFIG_INT(SvTurretFireCooldown, sv_turret_fire_cooldown, 30, 1, 600, CFGFLAG_SERVER, "Ticks between turret shots")

MACRO_CONFIG_INT(SvMysqlEnable, sv_mysql_enable, 0, 0, 1, CFGFLAG_SAVE | CFGFLAG_SERVER, "Enable MySQL account system")
MACRO_CONFIG_STR(SvMysqlHost, sv_mysql_host, 128, "127.0.0.1", CFGFLAG_SAVE | CFGFLAG_SERVER, "MySQL host")
MACRO_CONFIG_INT(SvMysqlPort, sv_mysql_port, 3306, 0, 65535, CFGFLAG_SAVE | CFGFLAG_SERVER, "MySQL TCP port (3306 default)")
MACRO_CONFIG_STR(SvMysqlUser, sv_mysql_user, 64, "teedefense", CFGFLAG_SAVE | CFGFLAG_SERVER, "MySQL user")
MACRO_CONFIG_STR(SvMysqlPassword, sv_mysql_password, 128, "", CFGFLAG_SAVE | CFGFLAG_SERVER, "MySQL password")
MACRO_CONFIG_STR(SvMysqlDatabase, sv_mysql_database, 64, "teedefense", CFGFLAG_SAVE | CFGFLAG_SERVER, "MySQL database name")
MACRO_CONFIG_INT(SvMysqlPoolSize, sv_mysql_pool_size, 4, 1, 32, CFGFLAG_SERVER, "MySQL connections in pool (job worker threads borrow)")

#endif
