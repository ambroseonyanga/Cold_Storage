import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

void main() {
  runApp(const ColdStorageApp());
}

class ColdStorageApp extends StatelessWidget {
  const ColdStorageApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Cold Storage',

      debugShowCheckedModeBanner: false,

      theme: ThemeData(useMaterial3: true),

      home: const DeviceScreen(),
    );
  }
}

class DeviceScreen extends StatefulWidget {
  const DeviceScreen({super.key});

  @override
  State<DeviceScreen> createState() => _DeviceScreenState();
}

class _DeviceScreenState extends State<DeviceScreen> {
  BluetoothDevice? connectedDevice;

  double temperature = 0;

  double setTemp = 0;

  double battery = 0;

  String tec = "OFF";

  // void startScan() {
  //   FlutterBluePlus.startScan(timeout: const Duration(seconds: 5));
  // }

  Future<void> startScan() async {
    await FlutterBluePlus.stopScan();

    FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
  }

  Future<void> connectToDevice(BluetoothDevice device) async {
    try {
      await device.connect();
    } catch (e) {
      // Device may already be connected
    }

    setState(() {
      connectedDevice = device;
    });

    discoverServices(device);
  }

  Future<void> discoverServices(BluetoothDevice device) async {
    List<BluetoothService> services = await device.discoverServices();

    for (BluetoothService service in services) {
      if (service.uuid.toString() == "12345678-1234-1234-1234-1234567890ab") {
        for (BluetoothCharacteristic characteristic
            in service.characteristics) {
          if (characteristic.uuid.toString() ==
              "abcd1234-5678-5678-5678-abcdef123456") {
            await characteristic.setNotifyValue(true);

            characteristic.lastValueStream.listen((value) {
              String text = utf8.decode(value);

              print("BLE DATA: $text");

              Map<String, dynamic> data = jsonDecode(text);

              setState(() {
                temperature = data["temperature"].toDouble();

                setTemp = data["setTemp"].toDouble();

                battery = data["battery"].toDouble();

                tec = data["tec"];
              });
            });
          }
        }
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("Cold Storage Monitor")),

      body: connectedDevice == null
          ? StreamBuilder<List<ScanResult>>(
              stream: FlutterBluePlus.scanResults,

              builder: (context, snapshot) {
                List<ScanResult> devices = snapshot.data ?? [];

                return Column(
                  children: [
                    const SizedBox(height: 20),

                    ElevatedButton(
                      onPressed: startScan,

                      child: const Text("Scan for ESP32"),
                    ),

                    const SizedBox(height: 20),

                    ElevatedButton(
                      onPressed: startScan,
                      child: const Text("Scan for ESP32"),
                    ),

                    const SizedBox(height: 10),

                    StreamBuilder<bool>(
                      stream: FlutterBluePlus.isScanning,
                      builder: (context, snapshot) {
                        final isScanning = snapshot.data ?? false;

                        return Text(
                          isScanning
                              ? "Scanning for Bluetooth devices..."
                              : "Press Scan for ESP32",
                        );
                      },
                    ),

                    Expanded(
                      child: ListView.builder(
                        itemCount: devices.length,

                        itemBuilder: (context, index) {
                          final device = devices[index].device;

                          // return ListTile(
                          //   title: Text(
                          //     device.platformName.isEmpty
                          //         ? "Unknown Device"
                          //         : device.platformName,
                          //   ),

                          //   subtitle: Text(device.remoteId.toString()),

                          //   onTap: () {
                          //     if (device.platformName == "ColdStorage-ESP32") {
                          //       connectToDevice(device);
                          //     }
                          //   },
                          // );

                          return ListTile(
                            title: Text(
                              device.platformName.isEmpty
                                  ? "Unknown Device"
                                  : device.platformName,
                            ),

                            subtitle: Text(device.remoteId.toString()),

                            trailing: const Icon(Icons.bluetooth),

                            onTap: () {
                              print("Selected device: ${device.platformName}");

                              connectToDevice(device);
                            },
                          );
                        },
                      ),
                    ),
                  ],
                );
              },
            )
          : Dashboard(
              temperature: temperature,

              setTemp: setTemp,

              battery: battery,

              tec: tec,
            ),
    );
  }
}

class Dashboard extends StatelessWidget {
  final double temperature;

  final double setTemp;

  final double battery;

  final String tec;

  const Dashboard({
    super.key,

    required this.temperature,

    required this.setTemp,

    required this.battery,

    required this.tec,
  });

  Widget buildCard(String title, String value) {
    return Card(
      margin: const EdgeInsets.all(12),

      child: Padding(
        padding: const EdgeInsets.all(20),

        child: Column(
          children: [
            Text(title, style: const TextStyle(fontSize: 18)),

            const SizedBox(height: 10),

            Text(
              value,

              style: const TextStyle(fontSize: 32, fontWeight: FontWeight.bold),
            ),
          ],
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ListView(
      children: [
        buildCard("🌡 Temperature", "${temperature.toStringAsFixed(1)} °C"),

        buildCard("🎯 Set Temperature", "${setTemp.toStringAsFixed(1)} °C"),

        buildCard("🔋 Battery", "${battery.toStringAsFixed(2)} V"),

        buildCard("❄️ TEC", tec),
      ],
    );
  }
}
