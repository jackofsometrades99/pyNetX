from ncclient import manager
import xml.etree.ElementTree as ET
import threading

# Define a list of NETCONF device information
devices = [
    {
        'host': '172.24.30.250',  # Replace with your first device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
    },
    {
        'host': '172.24.30.212',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.206',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.226',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.101',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.207',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.234',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.210',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.242',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.218',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    },
    {
        'host': '172.24.30.235',  # Replace with your second device's IP
        'port': "830",
        'username': 'ocnos',
        'password': 'ocnos',
        'hostkey_verify': False,
    }
    # Add more devices as needed
]

def subscribe_notifications(device):
    print(device)
    with manager.connect(
        **device
        # host=device["host"],
        # port=device["port"],
        # username=device["username"],
        # password=device["password"],
        # hostkey_verify=device["hostkey_verify"]
    ) as m:
        # Subscribe to notifications
        notification_filter = """
        <filter>
            <event xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-notifications"/>
        </filter>
        """

        # Create a subscription
        response = m.create_subscription(notification_filter)
        print(f"Subscription response for {device['host']}:", response)

        # Listen for notifications
        try:
            while True:
                notification = m.take_notification()
                xml_str = ET.tostring(notification.notification_ele, encoding='unicode')
                print(f"Received notification from {device['host']}:", xml_str)
        except KeyboardInterrupt:
            print(f"Unsubscribing from {device['host']}...")
            m.delete_subscription(response['subscription-id'])

if __name__ == "__main__":
    threads = []
    for device in devices:
        thread = threading.Thread(target=subscribe_notifications, args=(device,))
        thread.start()
        threads.append(thread)

    # Optionally join threads if you want to wait for them to finish
    for thread in threads:
        thread.join()