import time
import zenoh

config = zenoh.Config()
config.insert_json5(
    "connect/endpoints",
    '["tcp/127.0.0.1:7447"]',
)

def receive(sample):
    print(sample.key_expr, sample.payload)

with zenoh.open(config) as session:
    subscriber = session.declare_subscriber("can/*/rx", receive)
    print("Subscribed to can/*/rx")
    while True:
        time.sleep(1)
