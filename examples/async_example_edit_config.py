import asyncio
import sys
import os
import logging
import logging.config
import time
from pyNetX import NetconfClient

print(os.getcwd())
logging.config.fileConfig("logging.conf", disable_existing_loggers=False)
logger = logging.getLogger("example-app")

CONNECTIONS = {}

####################################################
# Per-device coroutine: connect, subscribe, receive
####################################################
async def handle_device(host: str, port: int, username: str, password: str):
    """Connect to one device and continuously receive notifications."""
    try:
        # 1) Connect (async)
        if host not in CONNECTIONS.keys():
            client = NetconfClient(host, port, username, password)
            try:
                connected = await client.connect_async()
                logger.info("Device %s:%d connected: %s", host, port, connected)
                CONNECTIONS[host] = client
            except Exception as error:
                logger.error(error)
                return
        else:
            client = CONNECTIONS[host]
            logger.info("Using saved connection object for %s", host)
        # 2) (Optional) get_config
        config_data = await client.get_config_async("running", "")
        logger.info("Device %s:%d get_config_async result: %s", host, port, config_data)

        # 3) Subscribe
        sub_reply = await client.subscribe_async("NETCONF", "")
        logger.info("Device %s:%d subscription reply: %s", host, port, sub_reply)

        # 4) Enter a loop to receive notifications
        logger.info("Device %s:%d: waiting for notifications...", host, port)
        count = 0
        while True:
            # This will block (cooperatively) until the device sends a notification
            if not CONNECTIONS[host]:
                logger.info("Client object for %s vanished")
                break
            logger.info("Client object for %s exists: %s", host, CONNECTIONS[host])
            try:
                full_notif = ""
                while True:
                    notif = "NO NOTIFICATION!"
                    notif = await client.receive_notification_async()
                    if notif == "NO NOTIFICATION!":
                        continue
                    if "]]>]]>" in notif:
                        full_notif += notif.split("]]>]]>")[0]
                        break
                    else:
                        full_notif += notif
                # file_path = os.path.join(
                #     "/home/sambhu/Documents/netconf_pybind/examples/notifications",
                #     f"{host}.txt"
                # )
                # with open(file_path, 'a+') as notif_file:
                #     notif_file.seek(0)
                #     current_data = notif_file.read()
                #     if notif != "NO NOTIFICATION!":
                #         current_data += notif
                #         notif_file.write(current_data)
                # logger.info("Data wrote to file %s.txt", host)
            except Exception as error:
                logger.error("This error occured when recieving notification for %s: %s", host, error)
            logger.info("Device %s:%d => Notification:\n%s", host, port, full_notif)
            time.sleep(2)
            # hostname = host.split(".")[-1]
            # if count >= 2:
            #     count = 0
            #     hostname = "NG-CSR"
            # config_data = await client.get_config_async("running", "")
            # logger.info("Device %s:%d get_config result: %s", host, port, config_data)
            # hostname_change_rpc = f"""
            #     <system-info xmlns="http://www.ipinfusion.com/yang/ocnos/ipi-system">
            #         <config>
            #             <hostname>{hostname}</hostname>
            #         </config>
            #     </system-info>
            # """
            # response = await client.locked_edit_config_async(
            #     target="candidate", config=hostname_change_rpc
            # )
            # logger.info("XML send for %s is %s", host, hostname_change_rpc)
            # logger.info("Response recieved for %s is %s", host, response)
            # count +=1
            # time.sleep(5)
    except asyncio.CancelledError:
        # This happens if we cancel the task (e.g., on shutdown).
        logger.info("Device %s:%d: task cancelled. Disconnecting...", host, port)
    except Exception as e:
        logger.exception("Device %s:%d: error occurred: %s", host, port, e)
    # finally:
    #     # 5) Disconnect if connected
    #     try:
    #         await client.disconnect_async()
    #         logger.info("Device %s:%d disconnected.", host, port)
    #     except Exception:
    #         pass  # might fail if never connected properly


####################################################
# Main: run multiple devices concurrently
####################################################
async def main():
    # Adjust your range or device list as needed
    # start_port = 5000
    # end_port = 5001  # shorter range for example
    hosts = [
        "172.24.30.250", "172.24.30.212", "172.24.30.206",
        "172.24.30.226", "172.24.30.101", "172.24.30.207",
        "172.24.30.234", "172.24.30.210", "172.24.30.242",
        "172.24.30.218", "172.24.30.235", "172.24.30.203"
    ]
    username = "ocnos"
    password = "ocnos"

    tasks = []
    for host in hosts:
        tasks.append(asyncio.create_task(
            handle_device(host, 830, username, password)
        ))

    logger.info("Launched %d device tasks. Press Ctrl+C to stop.", len(tasks))

    # Wait for all tasks forever, or until any of them raise an exception
    # If you want them all to run indefinitely, remove return_when=FIRST_EXCEPTION
    done, pending = await asyncio.wait(tasks) #return_when=asyncio.FIRST_EXCEPTION)

    # If any task died due to an exception, we can optionally cancel all others
    # for task in pending:
    #     task.cancel()

    logger.info("Main done, shutting down.")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Received KeyboardInterrupt. Exiting.")
