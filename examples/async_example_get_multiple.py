import threading
import asyncio
import sys
import os
import logging
import logging.config

logging.config.fileConfig("logging.conf", disable_existing_loggers=False)
logger = logging.getLogger("example-app")

build_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'build'))
if build_dir not in sys.path:
    sys.path.insert(0, build_dir)

from netconf_pybind import NetconfClient

##############################################################################
# List of devices
##############################################################################

devices = [
    {"hostname": "172.24.30.206", "port": 830, "username": "ocnos", "password": "ocnos"},
    {"hostname": "172.24.30.219", "port": 830, "username": "ocnos", "password": "ocnos"},
    {"hostname": "172.24.30.203", "port": 830, "username": "ocnos", "password": "ocnos"},
    {"hostname": "172.24.30.217", "port": 830, "username": "ocnos", "password": "ocnos"},
    {"hostname": "172.24.30.210", "port": 830, "username": "ocnos", "password": "ocnos"},
    {"hostname": "172.24.30.218", "port": 830, "username": "ocnos", "password": "ocnos"},
    # Add more devices as needed
]

##############################################################################
# Shared dictionary and lock
##############################################################################

# Key: (hostname, port)
# Value: NetconfClient object
connections = {}

# Protects reading/writing the 'connections' dict
connections_lock = threading.Lock()

##############################################################################
# 1. Connection Manager Thread (with its own asyncio loop)
##############################################################################

async def async_connect_device(device):
    """
    Create a NetconfClient, call connect_async(), return it upon success.
    Raise exception if connect fails.
    """
    client = NetconfClient(
        device["hostname"],
        device["port"],
        device["username"],
        device["password"]
    )
    # The library method is async, so we await it.
    res = await client.connect_async()  # spawns 2 short-lived C++ threads
    logger.info(f"[ConnManager] Connected: {device['hostname']}:{device['port']} -> {res}")
    return client

async def async_is_connection_alive(client):
    """
    Check if the Netconf session is alive.
    Minimal approach: call get_async() with an empty filter.
    If it fails or times out, we assume it's dead.
    """
    try:
        reply = await client.get_async("")
        # If we got a <rpc-reply> back, assume it's good
        return "<rpc-reply" in reply
    except Exception as e:
        logger.info(f"[ConnManager] is_connection_alive() error: {e}")
        return False

async def async_reconnect_device(device):
    """
    Disconnect old client (if any), then connect a new one.
    """
    key = (device["hostname"], device["port"])
    with connections_lock:
        old_client = connections.get(key)

    if old_client:
        try:
            await old_client.disconnect_async()  # spawns 2 short-lived C++ threads
        except Exception:
            pass

    new_client = await async_connect_device(device)
    with connections_lock:
        connections[key] = new_client

async def connection_manager_coroutine():
    """
    1) Connect all devices initially (store in 'connections')
    2) Loop forever: every 30s, check each connection.
       If it's not alive, reconnect.
    """
    # 1. Initial Connect
    for device in devices:
        key = (device["hostname"], device["port"])
        try:
            client = await async_connect_device(device)
            with connections_lock:
                connections[key] = client
        except Exception as e:
            logger.info(f"[ConnManager] Connect failed {key}: {e}")

    # 2. Periodic check every 30s
    while True:
        await asyncio.sleep(30)
        for device in devices:
            key = (device["hostname"], device["port"])
            with connections_lock:
                client = connections.get(key)
            if not client:
                # Possibly it failed earlier, attempt reconnect
                logger.info(f"[ConnManager] Missing client for {key}, reconnecting...")
                try:
                    await async_reconnect_device(device)
                except Exception as e:
                    logger.info(f"[ConnManager] Reconnect failed for {key}: {e}")
                continue

            # Check if alive
            alive = await async_is_connection_alive(client)
            if not alive:
                logger.info(f"[ConnManager] Connection lost for {key}, reconnecting...")
                try:
                    await async_reconnect_device(device)
                except Exception as e:
                    logger.info(f"[ConnManager] Reconnect failed for {key}: {e}")

def connection_manager_thread():
    """
    Target function for the 'ConnManager' Python thread.
    It creates an event loop and runs connection_manager_coroutine() forever.
    """
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        loop.run_until_complete(connection_manager_coroutine())
    finally:
        loop.close()

##############################################################################
# 2. Main Thread (with its own asyncio loop), fetch data every minute
##############################################################################

async def fetch_data(key, filter_xml=None):
    try:
        with connections_lock:
            client = connections.get(key)
        reply = await client.get_async(filter_xml)
        logger.info(f"[MainThread] Fetched data from {client}:\n{reply[:300]}...\n")
    # except netconf_pybind.NETCONFError as e:
    #     print(f"NETCONF Error: {e}")
    except Exception as e:
        logger.info(f"An unexpected error occurred: {e}")
    finally:
        await client.disconnect_async()

async def main_fetch_coroutine():
    """
    In a loop, every 60 seconds, do get_async() for each device using the
    NetconfClient from 'connections'.
    """
    while True:
        await asyncio.sleep(60)
        filter_xml = """
        <components xmlns="http://www.ipinfusion.com/yang/ocnos/ipi-platform">
           <component>
              <state>
                 <type>storage</type>
                 <memory />
              </state>
           </component>
        </components>
        """
        # For each device, do an async get
        clients = [(device["hostname"], device["port"]) for device in devices]
        tasks = []
        tasks = [fetch_data(key, filter_xml) for key in clients]
        try:
            # Each get_async spawns 2 short-lived C++ threads
            logger.info(f"Started batch")
            await asyncio.gather(*tasks)
        except Exception as e:
            logger.info(f"[MainThread] Error fetching from batch: {e}")
        
        # for device in devices:
        #     key = (device["hostname"], device["port"])
        #     with connections_lock:
        #         client = connections.get(key)
        #     if not client:
        #         logger.info(f"[MainThread] No client for {key}, skipping fetch.")
        #         continue

        #     try:
        #         # Each get_async spawns 2 short-lived C++ threads
        #         reply = await client.get_async(filter_xml)
        #         logger.info(f"[MainThread] Fetched data from {key}:\n{reply[:300]}...\n")
        #     except Exception as e:
        #         logger.info(f"[MainThread] Error fetching from {key}: {e}")

def main_fetch_loop():
    """
    The main thread function: create event loop,
    run main_fetch_coroutine() forever.
    """
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        loop.run_until_complete(main_fetch_coroutine())
    finally:
        loop.close()

##############################################################################
# 3. Program entry point
##############################################################################

def main():
    # Start connection manager in its own Python thread
    mgr_thread = threading.Thread(
        target=connection_manager_thread, daemon=True
    )
    mgr_thread.start()

    # Meanwhile, this main thread runs fetch loop
    main_fetch_loop()

if __name__ == "__main__":
    main()
