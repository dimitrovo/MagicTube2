package com.dimitrovo.magictubeshow

import android.app.Activity
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.*
import android.util.Log
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
//import com.dimitrovo.MagicTubeShow.R
import java.util.*

class MainActivity : AppCompatActivity() {

    private lateinit var freqText: TextView
    private lateinit var pickerHundreds: NumberPicker
    private lateinit var pickerTens: NumberPicker
    private lateinit var pickerOnes: NumberPicker
    private lateinit var setFreqButton: Button

    private var bluetoothGatt: BluetoothGatt? = null
    private val bleDeviceName = "MagicTube2"

    private val serviceUUID = UUID.fromString("12345678-1234-1234-1234-1234567890ab")
    private val charFreqUUID = UUID.fromString("abcd1111-1234-5678-1234-56789abcdef0")
    private val charSetFreqUUID = UUID.fromString("abcd2222-1234-5678-1234-56789abcdef0")

    private lateinit var statusText: TextView
    private var isConnected = false
    private var bleScanner: BluetoothLeScanner? = null

    private val scanCallback: ScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            result.device.connectGatt(this@MainActivity, false, gattCallback)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Підключення до елементів інтерфейсу
        statusText = findViewById(R.id.statusText)
        freqText = findViewById(R.id.frequencyText)
        pickerHundreds = findViewById(R.id.pickerHundreds)
        pickerTens = findViewById(R.id.pickerTens)
        pickerOnes = findViewById(R.id.pickerOnes)
        setFreqButton = findViewById(R.id.setFreqButton)

        // Обмеження NumberPicker'ів
        listOf(pickerHundreds, pickerTens, pickerOnes).forEach {
            it.minValue = 0
            it.maxValue = 9
        }

        // Кнопка надсилання частоти
        setFreqButton.setOnClickListener {
            val freq = pickerHundreds.value * 100 + pickerTens.value * 10 + pickerOnes.value
            sendFrequency(freq)
        }

        // Блокуємо UI поки не буде з'єднання
        updateUiEnabled(false)
        statusText.text = "🔌 Connecting to MagicTube2..."

        // 📛 Запит дозволів (Android 12+ вимагає нові)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requestPermissions(arrayOf(
                android.Manifest.permission.BLUETOOTH_SCAN,
                android.Manifest.permission.BLUETOOTH_CONNECT,
                android.Manifest.permission.ACCESS_FINE_LOCATION,
                android.Manifest.permission.ACCESS_COARSE_LOCATION
            ), 1)
        } else {
            requestPermissions(arrayOf(
                android.Manifest.permission.ACCESS_FINE_LOCATION,
                android.Manifest.permission.ACCESS_COARSE_LOCATION
            ), 1)
        }

        // ⏳ Почати BLE-сканування
        startBLEScan()
    }


//    private var bleScanner: BluetoothLeScanner? = null

    private fun startBLEScan() {
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val bluetoothAdapter = bluetoothManager.adapter
        bleScanner = bluetoothAdapter.bluetoothLeScanner

        // 🛑 Зупини попереднє сканування, якщо йшло
        bleScanner?.stopScan(scanCallback)

        // 📡 Фільтр по імені пристрою
        val filter = ScanFilter.Builder()
            .setDeviceName(bleDeviceName)
            .build()

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        bleScanner?.startScan(listOf(filter), settings, scanCallback)

        // ⏱ Зупинимо сканування через 10 секунд, якщо не знайдено
        Handler(Looper.getMainLooper()).postDelayed({
            if (!isConnected) {
                bleScanner?.stopScan(scanCallback)
                runOnUiThread {
                    statusText.text = "⚠️ Device not found"
                }
            }
        }, 10000)
    }




    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                bluetoothGatt = gatt
                isConnected = true

                bleScanner?.stopScan(scanCallback)

                runOnUiThread {
                    statusText.text = "✅ Connected!"
                    updateUiEnabled(true)
                }
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                isConnected = false
                runOnUiThread {
                    statusText.text = "⚠️ Disconnected. Reconnecting..."
                    updateUiEnabled(false)
                }
                bluetoothGatt?.close()

                Handler(Looper.getMainLooper()).postDelayed({
                    startBLEScan()
                }, 2000)
            }
        }


        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val service = gatt.getService(serviceUUID)
            val freqChar = service?.getCharacteristic(charFreqUUID)

            if (freqChar != null) {
                gatt.setCharacteristicNotification(freqChar, true)
                val descriptor = freqChar.getDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"))
                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(descriptor)

                // ✅ BLE повністю готове — оновлюємо UI
                runOnUiThread {
                    statusText.text = "✅ Connected!"
                    updateUiEnabled(true)
                }
            } else {
                runOnUiThread {
                    statusText.text = "❌ Characteristic not found"
                }
            }
        }


        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid == charFreqUUID) {
                val value = characteristic.getStringValue(0)
                runOnUiThread {
                    freqText.text = "$value Hz"
                }
            }
        }
    }

    private fun sendFrequency(freq: Int) {
        val char = bluetoothGatt
            ?.getService(serviceUUID)
            ?.getCharacteristic(charSetFreqUUID)

        char?.value = freq.toString().toByteArray()
        bluetoothGatt?.writeCharacteristic(char)
        Toast.makeText(this, "Sent $freq Hz", Toast.LENGTH_SHORT).show()
    }

    private fun updateUiEnabled(enabled: Boolean) {
        listOf(pickerHundreds, pickerTens, pickerOnes, setFreqButton).forEach {
            it.isEnabled = enabled
        }
    }

    override fun onDestroy() {
        bluetoothGatt?.close()
        super.onDestroy()
    }
}
