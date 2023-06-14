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

mydb = client["CE232_Lab"]
mycol = mydb["Lab5_Nhom7"]

def on_message(client_mqtt, userdata, message):
    print("received message =", str(message.payload.decode("utf-8")))
    doc = {}
    data = message.payload.decode()
    temperature, humidity = data.split(',')
    temperature = float(temperature)
    humidity = float(humidity)
    doc['temperature'] = temperature
    doc['humidity'] = humidity
    doc['timestamp'] = str(datetime.now())
    doc['timezone'] = str(datetime.now().astimezone().tzinfo)
    mycol.insert_one(doc)

# Connect to MQTT broker with username and password if needed
broker_address = "410e88d4c6f74067af21eb2712fbc8a4.s2.eu.hivemq.cloud"
client_mqtt = paho.Client(client_id="", userdata=None, protocol=paho.MQTTv5)
client_mqtt.on_message = on_message
client_mqtt.tls_set(tls_version=mqtt.client.ssl.PROTOCOL_TLS)
client_mqtt.username_pw_set(username="dataprocess", password="&447s92E")
client_mqtt.connect(broker_address, port=8883)
client_mqtt.subscribe("temperature_humidity/data")


client_mqtt.loop_forever()