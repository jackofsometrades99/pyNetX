# examples/example.py

import sys
import os
from lxml import etree

# Adjust the path to include the build directory
sys.path.append(os.path.abspath("../build"))

import netconf_pybind

COMPONENTS_GLOBAL_DASH_FIL = """
<components xmlns="http://www.ipinfusion.com/yang/ocnos/ipi-platform">
   <component>
      <state>
         <type>storage</type>
         <memory />
      </state>
   </component>
   <component>
      <state>
         <type>ram</type>
         <memory />
      </state>
   </component>
   <component>
      <cpu>
         <state>
            <cpu-utilization />
         </state>
      </cpu>
   </component>
</components>
"""

def main():
    # NETCONF device credentials and details
    hostname = "172.24.30.206"      # Replace with your NETCONF device IP
    port = 830                   # Default NETCONF SSH port
    username = "ocnos"           # Replace with your username
    password = "ocnos"        # Replace with your password

    # Initialize the NETCONF client
    client = netconf_pybind.NetconfClient(hostname, port, username, password)

    try:
        # Connect to the NETCONF device
        client.connect()
        print("Connected to the NETCONF device.")

        # Perform a <get> operation without a filter
        response = client.get(COMPONENTS_GLOBAL_DASH_FIL)
        print("NETCONF <get> Response:")
        print(response)

        # Optionally, perform a <get> operation with a filter
        # filter_xml = """
        # <filter type="subtree" xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">
        #     <interfaces xmlns="urn:ietf:params:xml:ns:yang:ietf-interfaces"/>
        # </filter>
        # """
        # response_filtered = client.get(filter_xml)
        # print("Filtered NETCONF <get> Response:")
        # print(response_filtered)

    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        # Disconnect from the NETCONF device
        client.disconnect()
        print("Disconnected from the NETCONF device.")

if __name__ == "__main__":
    main()
