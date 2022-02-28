package edu.hm.karbaumer.lenz.android_kvm_hello_world

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import edu.hm.karbaumer.lenz.android_kvm_hello_world.databinding.ActivityMainBinding
import android.content.res.AssetManager
import android.view.View


class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private lateinit var mgr: AssetManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        mgr = resources.assets
        binding.cppOutput.text = kvmHelloWorld(mgr)
    }

    fun kvmRun(view: View) {
        binding.cppOutput.text = getKvmHelloWorldLog()
    }

    /**
     * A native method that is implemented by the 'android_kvm_hello_world' native library,
     * which is packaged with this application.
     */
    external fun kvmHelloWorld(mgr: AssetManager): String

    external fun getKvmHelloWorldLog(): String

    companion object {
        // Used to load the 'android_kvm_hello_world' library on application startup.
        init {
            System.loadLibrary("android_kvm_hello_world")
        }
    }
}