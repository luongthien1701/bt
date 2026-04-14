const express = require("express");
const mqtt = require("mqtt");
const cors = require("cors");

const app = express();
app.use(cors());
app.use(express.json());

const client = mqtt.connect("mqtt://broker.hivemq.com");

let logs = [];
let status = "CLOSE";
let mpuData = {};

// ===== MQTT CONNECT =====
client.on("connect", () => {
  console.log("MQTT connected");
  client.subscribe("safe/#");
});

// ===== MQTT MESSAGE =====
client.on("message", (topic, message) => {
  const msg = message.toString();

  console.log(topic + ": " + msg);

  if (topic === "safe/status") {
    status = msg;
  }

  if (topic === "safe/log" || topic === "safe/alert") {
    logs.unshift({
      time: new Date().toLocaleString(),
      topic,
      message: msg,
    });

    logs = logs.slice(0, 50);
  }

  if (topic === "safe/mpu") {
    try {
      mpuData = JSON.parse(msg);
    } catch {
      console.log("MPU parse error");
    }
  }
});

// ===== API =====
app.get("/mpu", (req, res) => {
  res.json(mpuData); 
});

app.get("/status", (req, res) => {
  res.json({ status });
});

app.get("/logs", (req, res) => {
  res.json(logs);
});

app.post("/control", (req, res) => {
  const { cmd } = req.body;

  if (cmd === "OPEN" || cmd === "CLOSE") {
    client.publish("safe/control", cmd, { retain: false });

    return res.send("OK");
  }

  res.status(400).send("Invalid");
});

// ===== START =====
app.listen(3000, () => {
  console.log("Server running: http://localhost:3000");
});