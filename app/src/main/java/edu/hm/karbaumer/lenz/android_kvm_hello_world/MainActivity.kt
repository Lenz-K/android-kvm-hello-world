package edu.hm.karbaumer.lenz.android_kvm_hello_world

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import edu.hm.karbaumer.lenz.android_kvm_hello_world.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Example of a call to a native method
        binding.vmOutput.text = kvmHelloWorld()

        binding.cppOutput.text = getKvmHelloWorldLog()
    }

    /**
     * A native method that is implemented by the 'android_kvm_hello_world' native library,
     * which is packaged with this application.
     */
    external fun kvmHelloWorld(): String

    external fun getKvmHelloWorldLog(): String

    companion object {
        // Used to load the 'android_kvm_hello_world' library on application startup.
        init {
            System.loadLibrary("android_kvm_hello_world")
        }
    }
}