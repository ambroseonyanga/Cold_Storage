import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

void main() {
  runApp(const ColdStorageApp());
}

// ============================================================
// APPLICATION
// ============================================================

class ColdStorageApp extends StatelessWidget {
  const ColdStorageApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Cold Storage Monitor',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(useMaterial3: true, colorSchemeSeed: Colors.blue),
      home: const DeviceScreen(),
    );
  }
}

// ============================================================
// DEVICE SCREEN
// ============================================================

class DeviceScreen extends StatefulWidget {
  const DeviceScreen({super.key});

  @override
  State<DeviceScreen> createState() => _DeviceScreenState();
}

class _DeviceScreenState extends State<DeviceScreen> {
  // ----------------------------------------------------------
  // BLE UUIDs
  // ----------------------------------------------------------

  static const String serviceUuid = "12345678-1234-1234-1234-1234567890ab";

  BluetoothCharacteristic? controlCharacteristic;

  static const String characteristicUuid =
      "abcd1234-5678-5678-5678-abcdef123456";

  Future<void> sendCommand(String command) async {
    if (controlCharacteristic == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Not connected to the ESP32")),
      );
      return;
    }

    try {
      debugPrint("Sending BLE command: $command");

      await controlCharacteristic!.write(
        utf8.encode(command),
        withoutResponse: false,
      );

      debugPrint("Command sent successfully");
    } catch (e) {
      debugPrint("Command failed: $e");

      if (!mounted) return;

      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text("Command failed: $e")));
    }
  }

  // ----------------------------------------------------------
  // BLE STATE
  // ----------------------------------------------------------

  BluetoothDevice? connectedDevice;

  StreamSubscription<List<int>>? notificationSubscription;

  StreamSubscription<BluetoothConnectionState>? connectionSubscription;

  bool isConnecting = false;

  String connectionStatus = "Not connected";

  // ----------------------------------------------------------
  // SENSOR DATA
  // ----------------------------------------------------------

  double temperature = 0;

  double setTemp = 0;

  double battery = 0;

  String tec = "OFF";

  // ============================================================
  // START SCAN
  // ============================================================

  Future<void> startScan() async {
    try {
      debugPrint("================================");
      debugPrint("Starting BLE scan...");
      debugPrint("================================");

      await FlutterBluePlus.stopScan();

      setState(() {
        connectionStatus = "Scanning...";
      });

      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
    } catch (e) {
      debugPrint("Scan error: $e");

      if (!mounted) return;

      setState(() {
        connectionStatus = "Scan error";
      });

      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text("Scan error: $e")));
    }
  }

  // ============================================================
  // CONNECT TO DEVICE
  // ============================================================

  Future<void> connectToDevice(BluetoothDevice device) async {
    if (isConnecting) return;

    try {
      setState(() {
        isConnecting = true;
        connectionStatus = "Connecting...";
      });

      await FlutterBluePlus.stopScan();

      debugPrint("================================");
      debugPrint("Connecting to ${device.platformName}");
      debugPrint("Device ID: ${device.remoteId}");
      debugPrint("================================");

      // --------------------------------------------------------
      // CONNECT
      // --------------------------------------------------------

      await device.connect(
        timeout: const Duration(seconds: 15),
        autoConnect: false,
      );

      debugPrint("Successfully connected!");

      // --------------------------------------------------------
      // MONITOR CONNECTION STATE
      // --------------------------------------------------------

      await connectionSubscription?.cancel();

      connectionSubscription = device.connectionState.listen((
        BluetoothConnectionState state,
      ) {
        debugPrint("Connection state: $state");

        if (!mounted) return;

        if (state == BluetoothConnectionState.connected) {
          setState(() {
            connectionStatus = "Connected";
          });
        }

        if (state == BluetoothConnectionState.disconnected) {
          debugPrint("Device disconnected");

          notificationSubscription?.cancel();

          setState(() {
            connectedDevice = null;

            temperature = 0;
            setTemp = 0;
            battery = 0;
            tec = "OFF";

            connectionStatus = "Disconnected";
          });
        }
      });

      // --------------------------------------------------------
      // SET CONNECTED DEVICE
      // --------------------------------------------------------

      if (!mounted) return;

      setState(() {
        connectedDevice = device;
        connectionStatus = "Connected";
      });

      // --------------------------------------------------------
      // DISCOVER SERVICES
      // --------------------------------------------------------

      await discoverServices(device);
    } catch (e) {
      debugPrint("================================");
      debugPrint("CONNECTION FAILED");
      debugPrint("$e");
      debugPrint("================================");

      if (!mounted) return;

      setState(() {
        connectedDevice = null;
        connectionStatus = "Connection failed";
      });

      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text("Connection failed: $e")));
    } finally {
      if (mounted) {
        setState(() {
          isConnecting = false;
        });
      }
    }
  }

  // ============================================================
  // DISCOVER BLE SERVICES
  // ============================================================

  Future<void> discoverServices(BluetoothDevice device) async {
    try {
      debugPrint("================================");
      debugPrint("Discovering services...");
      debugPrint("================================");

      List<BluetoothService> services = await device.discoverServices();

      debugPrint("Services found: ${services.length}");

      bool serviceFound = false;

      bool characteristicFound = false;

      // --------------------------------------------------------
      // LOOP THROUGH SERVICES
      // --------------------------------------------------------

      for (BluetoothService service in services) {
        debugPrint("Service UUID: ${service.uuid}");

        if (service.uuid.toString().toLowerCase() ==
            serviceUuid.toLowerCase()) {
          serviceFound = true;

          debugPrint("✓ CORRECT SERVICE FOUND!");

          // ----------------------------------------------------
          // LOOP THROUGH CHARACTERISTICS
          // ----------------------------------------------------

          for (BluetoothCharacteristic characteristic
              in service.characteristics) {
            debugPrint(
              "Characteristic UUID: "
              "${characteristic.uuid}",
            );

            if (characteristic.uuid.toString().toLowerCase() ==
                characteristicUuid.toLowerCase()) {
              characteristicFound = true;
              controlCharacteristic = characteristic;

              debugPrint("✓ CORRECT CHARACTERISTIC FOUND!");

              debugPrint(
                "Read: "
                "${characteristic.properties.read}",
              );

              debugPrint(
                "Notify: "
                "${characteristic.properties.notify}",
              );

              // ------------------------------------------------
              // ENABLE NOTIFICATIONS
              // ------------------------------------------------

              if (characteristic.properties.notify) {
                await characteristic.setNotifyValue(true);

                debugPrint("✓ Notifications enabled");
              } else {
                debugPrint("❌ Characteristic does not support notifications");
              }

              // ------------------------------------------------
              // CANCEL OLD SUBSCRIPTION
              // ------------------------------------------------

              await notificationSubscription?.cancel();

              // ------------------------------------------------
              // LISTEN FOR ESP32 DATA
              // ------------------------------------------------

              notificationSubscription = characteristic.lastValueStream.listen(
                (List<int> value) {
                  if (value.isEmpty) {
                    return;
                  }

                  try {
                    // Convert bytes to text
                    String text = utf8.decode(value);

                    debugPrint("================================");

                    debugPrint("BLE DATA RECEIVED:");

                    debugPrint(text);

                    debugPrint("================================");

                    // Decode JSON
                    Map<String, dynamic> data = jsonDecode(text);

                    if (!mounted) {
                      return;
                    }

                    // Update dashboard
                    setState(() {
                      temperature =
                          (data["temperature"] as num?)?.toDouble() ?? 0;

                      setTemp = (data["setTemp"] as num?)?.toDouble() ?? 0;

                      battery = (data["battery"] as num?)?.toDouble() ?? 0;

                      tec = data["tec"]?.toString() ?? "OFF";
                    });

                    debugPrint("Temperature: $temperature");

                    debugPrint("Set Temp: $setTemp");

                    debugPrint("Battery: $battery");

                    debugPrint("TEC: $tec");
                  } catch (e) {
                    debugPrint("❌ Error decoding BLE data: $e");
                  }
                },
                onError: (error) {
                  debugPrint("❌ BLE notification error: $error");
                },
              );

              // ------------------------------------------------
              // OPTIONAL INITIAL READ
              // ------------------------------------------------

              if (characteristic.properties.read) {
                try {
                  List<int> value = await characteristic.read();

                  if (value.isNotEmpty) {
                    String text = utf8.decode(value);

                    debugPrint("Initial BLE value: $text");
                  }
                } catch (e) {
                  debugPrint("Initial read error: $e");
                }
              }
            }
          }
        }
      }

      // --------------------------------------------------------
      // ERROR CHECKING
      // --------------------------------------------------------

      if (!serviceFound) {
        debugPrint("❌ COLD STORAGE SERVICE NOT FOUND");

        debugPrint("Expected service UUID:");

        debugPrint(serviceUuid);
      }

      if (!characteristicFound) {
        debugPrint("❌ COLD STORAGE CHARACTERISTIC NOT FOUND");

        debugPrint("Expected characteristic UUID:");

        debugPrint(characteristicUuid);
      }

      if (serviceFound && characteristicFound) {
        debugPrint("================================");

        debugPrint("✓ BLE SETUP COMPLETE");

        debugPrint("Waiting for ESP32 notifications...");

        debugPrint("================================");
      }
    } catch (e) {
      debugPrint("❌ Service discovery error: $e");

      if (!mounted) return;

      setState(() {
        connectionStatus = "Service discovery failed";
      });
    }
  }

  // ============================================================
  // DISCONNECT DEVICE
  // ============================================================

  Future<void> disconnectDevice() async {
    if (connectedDevice == null) {
      return;
    }

    try {
      debugPrint("Disconnecting device...");

      await notificationSubscription?.cancel();

      notificationSubscription = null;

      await connectionSubscription?.cancel();

      connectionSubscription = null;

      await connectedDevice!.disconnect();

      debugPrint("Device disconnected successfully");
    } catch (e) {
      debugPrint("Disconnect error: $e");
    }

    if (!mounted) return;

    setState(() {
      connectedDevice = null;

      temperature = 0;

      setTemp = 0;

      battery = 0;

      tec = "OFF";

      connectionStatus = "Disconnected";
    });
  }

  // ============================================================
  // CLEANUP
  // ============================================================

  @override
  void dispose() {
    notificationSubscription?.cancel();

    connectionSubscription?.cancel();

    if (connectedDevice != null) {
      connectedDevice!.disconnect();
    }

    super.dispose();
  }

  // ============================================================
  // BUILD
  // ============================================================

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Cold Storage Monitor"),

        centerTitle: true,

        actions: [
          if (connectedDevice != null)
            IconButton(
              icon: const Icon(Icons.bluetooth_disabled),

              tooltip: "Disconnect",

              onPressed: disconnectDevice,
            ),
        ],
      ),

      body: connectedDevice == null
          ? buildScanScreen()
          : Dashboard(
              deviceName: connectedDevice!.platformName.isEmpty
                  ? "ColdStorage-ESP32"
                  : connectedDevice!.platformName,

              temperature: temperature,

              setTemp: setTemp,

              battery: battery,

              tec: tec,

              connectionStatus: connectionStatus,

              onDisconnect: disconnectDevice,

              sendCommand: sendCommand,
            ),
    );
  }

  // ============================================================
  // SCAN SCREEN
  // ============================================================

  Widget buildScanScreen() {
    return StreamBuilder<List<ScanResult>>(
      stream: FlutterBluePlus.scanResults,

      builder: (context, snapshot) {
        List<ScanResult> devices = snapshot.data ?? [];

        return Column(
          children: [
            const SizedBox(height: 25),

            // --------------------------------------------------
            // SCAN BUTTON
            // --------------------------------------------------
            ElevatedButton.icon(
              onPressed: isConnecting ? null : startScan,

              icon: const Icon(Icons.search),

              label: const Text("Scan for ESP32"),
            ),

            const SizedBox(height: 15),

            // --------------------------------------------------
            // SCANNING STATUS
            // --------------------------------------------------
            StreamBuilder<bool>(
              stream: FlutterBluePlus.isScanning,

              builder: (context, snapshot) {
                final bool isScanning = snapshot.data ?? false;

                return Column(
                  children: [
                    if (isScanning) const CircularProgressIndicator(),

                    const SizedBox(height: 10),

                    Text(
                      isScanning
                          ? "Scanning for Bluetooth devices..."
                          : connectionStatus,
                    ),
                  ],
                );
              },
            ),

            const SizedBox(height: 15),

            const Divider(),

            // --------------------------------------------------
            // DEVICE LIST
            // --------------------------------------------------
            Expanded(
              child: devices.isEmpty
                  ? const Center(
                      child: Text(
                        "No devices found yet\n\n"
                        "Make sure your ESP32 is powered on\n"
                        "and BLE advertising is running.",
                        textAlign: TextAlign.center,
                      ),
                    )
                  : ListView.builder(
                      itemCount: devices.length,

                      itemBuilder: (context, index) {
                        final result = devices[index];

                        final device = result.device;

                        String name = device.platformName;

                        if (name.isEmpty) {
                          name = result.advertisementData.advName;
                        }

                        if (name.isEmpty) {
                          name = "Unknown Device";
                        }

                        return Card(
                          margin: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 6,
                          ),

                          child: ListTile(
                            leading: const Icon(Icons.bluetooth, size: 32),

                            title: Text(name),

                            subtitle: Text(device.remoteId.toString()),

                            trailing: ElevatedButton(
                              onPressed: isConnecting
                                  ? null
                                  : () {
                                      connectToDevice(device);
                                    },

                              child: isConnecting
                                  ? const SizedBox(
                                      width: 20,
                                      height: 20,

                                      child: CircularProgressIndicator(
                                        strokeWidth: 2,
                                      ),
                                    )
                                  : const Text("Connect"),
                            ),
                          ),
                        );
                      },
                    ),
            ),
          ],
        );
      },
    );
  }
}

// ============================================================
// DASHBOARD
// ============================================================

class Dashboard extends StatelessWidget {
  final Future<void> Function(String command) sendCommand;

  final String deviceName;

  final double temperature;

  final double setTemp;

  final double battery;

  final String tec;

  final String connectionStatus;

  final VoidCallback onDisconnect;

  const Dashboard({
    super.key,

    required this.deviceName,

    required this.temperature,

    required this.setTemp,

    required this.battery,

    required this.tec,

    required this.connectionStatus,

    required this.onDisconnect,

    required this.sendCommand,
  });

  // ============================================================
  // DASHBOARD CARD
  // ============================================================

  // Widget buildCard(String title, String value, IconData icon) {
  //   return Card(
  //     margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),

  //     elevation: 2,

  //     child: Padding(
  //       padding: const EdgeInsets.all(20),

  //       child: Row(
  //         children: [
  //           Icon(icon, size: 42),

  //           const SizedBox(width: 20),

  //           Expanded(
  //             child: Column(
  //               crossAxisAlignment: CrossAxisAlignment.start,

  //               children: [
  //                 Text(title, style: const TextStyle(fontSize: 16)),

  //                 const SizedBox(height: 6),

  //                 Text(
  //                   value,

  //                   style: const TextStyle(
  //                     fontSize: 28,
  //                     fontWeight: FontWeight.bold,
  //                   ),
  //                 ),
  //               ],
  //             ),
  //           ),
  //         ],
  //       ),
  //     ),
  //   );
  // }

  Widget buildCard(String title, String value, IconData icon) {
    return Expanded(
      child: Card(
        margin: const EdgeInsets.all(4),
        elevation: 2,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 10),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, size: 24),

              const SizedBox(height: 5),

              Text(
                title,
                textAlign: TextAlign.center,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontSize: 11),
              ),

              const SizedBox(height: 4),

              Text(
                value,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  fontSize: 17,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  // ============================================================
  // BUILD
  // ============================================================

  @override
  Widget build(BuildContext context) {
    return ListView(
      // padding: const EdgeInsets.only(top: 25),
      padding: const EdgeInsets.only(top: 10),

      children: [
        // ------------------------------------------------------
        // DEVICE NAME
        // ------------------------------------------------------

        Center(
          child: Column(
            children: [
              const Icon(Icons.bluetooth_connected, size: 50),

              const SizedBox(height: 10),

              Text("Connected to", style: const TextStyle(fontSize: 15)),

              const SizedBox(height: 5),

              Text(
                deviceName,

                style: const TextStyle(
                  fontSize: 20,
                  fontWeight: FontWeight.bold,
                ),
              ),

              const SizedBox(height: 5),

              Text(connectionStatus),
            ],
          ),
        ),

        // const SizedBox(height: 20),
        const SizedBox(height: 10),

        // // ------------------------------------------------------
        // // TEMPERATURE
        // // ------------------------------------------------------
        // buildCard(
        //   "Current Temperature",

        //   "${temperature.toStringAsFixed(1)} °C",

        //   Icons.thermostat,
        // ),

        // // ------------------------------------------------------
        // // SET TEMPERATURE
        // // ------------------------------------------------------
        // buildCard(
        //   "Set Temperature",

        //   "${setTemp.toStringAsFixed(1)} °C",

        //   Icons.tune,
        // ),

        // // ------------------------------------------------------
        // // BATTERY
        // // ------------------------------------------------------
        // buildCard(
        //   "Battery Voltage",

        //   "${battery.toStringAsFixed(2)} V",

        //   Icons.battery_full,
        // ),

        // // ------------------------------------------------------
        // // TEC
        // // ------------------------------------------------------
        // buildCard("TEC Status", tec, Icons.ac_unit),

        // const SizedBox(height: 25),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: Row(
            children: [
              buildCard(
                "Temperature",
                "${temperature.toStringAsFixed(1)} °C",
                Icons.thermostat,
              ),

              buildCard(
                "Set Temp",
                "${setTemp.toStringAsFixed(1)} °C",
                Icons.tune,
              ),

              buildCard(
                "Battery",
                "${battery.toStringAsFixed(2)} V",
                Icons.battery_full,
              ),

              buildCard("TEC", tec, Icons.ac_unit),
            ],
          ),
        ),
        // ----------------------------------------------------------------------
        //DASHBOARD CONROLS
        // ----------------------------------------------------------------------
        const SizedBox(height: 20),

        const Padding(
          padding: EdgeInsets.symmetric(horizontal: 16),
          child: Text(
            "Operating Mode",
            style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
          ),
        ),

        const SizedBox(height: 10),

        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            children: [
              Expanded(
                child: ElevatedButton(
                  onPressed: () {
                    sendCommand("MODE_OFF");
                  },
                  child: const Text("OFF"),
                ),
              ),

              const SizedBox(width: 8),

              Expanded(
                child: ElevatedButton(
                  onPressed: () {
                    sendCommand("MODE_AUTO");
                  },
                  child: const Text("AUTO"),
                ),
              ),

              const SizedBox(width: 8),

              Expanded(
                child: ElevatedButton(
                  onPressed: () {
                    sendCommand("MODE_ON");
                  },
                  child: const Text("ON"),
                ),
              ),
            ],
          ),
        ),

        const SizedBox(height: 25),

        const Padding(
          padding: EdgeInsets.symmetric(horizontal: 16),
          child: Text(
            "Temperature Control",
            style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
          ),
        ),

        const SizedBox(height: 10),

        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              IconButton(
                iconSize: 40,
                onPressed: () {
                  final newTemp = setTemp - 0.5;

                  if (newTemp >= -10.0) {
                    sendCommand("SET_TEMP:${newTemp.toStringAsFixed(1)}");
                  }
                },
                icon: const Icon(Icons.remove_circle),
              ),

              const SizedBox(width: 20),

              Text(
                "${setTemp.toStringAsFixed(1)} °C",
                style: const TextStyle(
                  fontSize: 30,
                  fontWeight: FontWeight.bold,
                ),
              ),

              const SizedBox(width: 20),

              IconButton(
                iconSize: 40,
                onPressed: () {
                  final newTemp = setTemp + 0.5;

                  if (newTemp <= 50.0) {
                    sendCommand("SET_TEMP:${newTemp.toStringAsFixed(1)}");
                  }
                },
                icon: const Icon(Icons.add_circle),
              ),
            ],
          ),
        ),

        const SizedBox(height: 25),

        const Padding(
          padding: EdgeInsets.symmetric(horizontal: 16),
          child: Text(
            "TEC Control",
            style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
          ),
        ),

        const SizedBox(height: 10),

        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            children: [
              Expanded(
                child: ElevatedButton.icon(
                  onPressed: () {
                    sendCommand("TEC_ON");
                  },
                  icon: const Icon(Icons.power),
                  label: const Text("TEC ON"),
                ),
              ),

              const SizedBox(width: 10),

              Expanded(
                child: ElevatedButton.icon(
                  onPressed: () {
                    sendCommand("TEC_OFF");
                  },
                  icon: const Icon(Icons.power_off),
                  label: const Text("TEC OFF"),
                ),
              ),
            ],
          ),
        ),
        // ----------------------------------------------------------------------
        //END DASHBOARD CONTROLS

        // ------------------------------------------------------
        // DISCONNECT BUTTON
        // ------------------------------------------------------
        Padding(
          padding: const EdgeInsets.all(16),

          child: ElevatedButton.icon(
            onPressed: onDisconnect,

            icon: const Icon(Icons.bluetooth_disabled),

            label: const Text("Disconnect"),
          ),
        ),

        const SizedBox(height: 20),
      ],
    );
  }
}
