package com.dimitrovo.magictubeshow

import android.os.Bundle
/*import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import com.dimitrovo.magictubeshow.ui.theme.MagicTubeShowTheme*/
import androidx.appcompat.app.AppCompatActivity

import android.widget.Button
import android.widget.NumberPicker
import android.widget.TextView
import android.widget.Toast

class MainActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)  // зв’язуємо XML

        freqText = findViewById(R.id.frequencyText)
        pickerHundreds = findViewById(R.id.pickerHundreds)
        pickerTens = findViewById(R.id.pickerTens)
        pickerOnes = findViewById(R.id.pickerOnes)
        setFreqButton = findViewById(R.id.setFreqButton)

// встанови діапазони
        listOf(pickerHundreds, pickerTens, pickerOnes).forEach {
            it.minValue = 0
            it.maxValue = 9
        }

// кнопка для надсилання частоти
        setFreqButton.setOnClickListener {
            val freq = pickerHundreds.value * 100 + pickerTens.value * 10 + pickerOnes.value
            Toast.makeText(this, "Selected frequency: $freq Hz", Toast.LENGTH_SHORT).show()
        }



    }

    private lateinit var freqText: TextView
    private lateinit var pickerHundreds: NumberPicker
    private lateinit var pickerTens: NumberPicker
    private lateinit var pickerOnes: NumberPicker
    private lateinit var setFreqButton: Button



}


