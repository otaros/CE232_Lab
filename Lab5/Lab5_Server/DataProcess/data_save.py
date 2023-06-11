import os
import json
from datetime import datetime
import paho.mqtt.client as paho
from paho import mqtt
from pymongo.mongo_client import MongoClient
from pymongo.server_api import ServerApi

# set time zone to Bangkok/Hanoi/Jakarta
os.environ['TZ'] = 'Asia/Bangkok'

# Create a new client and connect to the server
uri = "mongodb+srv://otaros0:CP5OhD7CRWovQghi@cluster0.e8yflsz.mongodb.net/?retryWrites=true&w=majority"
client = MongoClient(uri, server_api=ServerApi("1"))

mydb = client["mydatabase"]
mycol = mydb["customers"]

def on_message(client_mqtt, userdata, message):
    print("received message =", str(message.payload.decode("utf-8")))
    doc = json.loads(str(message.payload.decode("utf-8")))
    doc['timestamp'] = str(datetime.now())
    doc['timezone'] = str(datetime.now().astimezone().tzinfo)
    mycol.insert_one(doc)

# Connect to MQTT broker with username and password if needed
broker_address = "410e88d4c6f74067af21eb2712fbc8a4.s2.eu.hivemq.cloud"
client_mqtt = paho.Client(client_id="", userdata=None, protocol=paho.MQTTv5)
client_mqtt.on_message = on_message
client_mqtt.tls_set(tls_version=mqtt.client.ssl.PROTOCOL_TLS)
client_mqtt.username_pw_set(username="pchost", password="&447s92E")
client_mqtt.connect(broker_address, port=8883)
client_mqtt.subscribe("test", qos=1)


# Send a ping to confirm a successful connection
for db_name in client.list_database_names():
    print(db_name)

client_mqtt.loop_forever()