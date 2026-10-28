package org.dmc3port;

import android.content.Context;
import android.content.Intent;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;

import java.io.File;

import org.libsdl.app.SDLActivity;

// Atividade do jogo: o SDLActivity carrega libSDL3.so e libmain.so (o jogo recompilado + runtime) e chama o main.
public class DMC3Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    // pasta do jogo na memória (Memória interna/DMC3): arquivos do jogo, saves e log ficam aqui, fora da pasta do app,
    // para sobreviver a uma desinstalação e serem fáceis de copiar (android_setup.cpp)
    public static String getGameFolder() {
        return new File(Environment.getExternalStorageDirectory(), "DMC3").getAbsolutePath();
    }

    // "acesso a todos os arquivos" (Android 11+), necessário para usar a pasta DMC3
    public static boolean hasAllFilesAccess() {
        return Environment.isExternalStorageManager();
    }

    // abre a tela do sistema onde o usuário libera o acesso para este app
    public static void requestAllFilesAccess() {
        Context context = getContext();
        if (context == null) {
            return;
        }
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + context.getPackageName()));
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(intent);
        } catch (Exception e) {
            Intent all = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
            all.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(all);
        }
    }

    // chamado pelo áudio do jogo (kernel_audio.cpp): true se o som sai pelo alto-falante do aparelho, ou seja, não há
    // fone com fio, USB ou Bluetooth de áudio conectado
    public static boolean isSpeakerOutput() {
        Context context = getContext();
        if (context == null) {
            return true;
        }
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (audio == null) {
            return true;
        }
        for (AudioDeviceInfo device : audio.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            switch (device.getType()) {
                case AudioDeviceInfo.TYPE_WIRED_HEADPHONES:
                case AudioDeviceInfo.TYPE_WIRED_HEADSET:
                case AudioDeviceInfo.TYPE_BLUETOOTH_A2DP:
                case AudioDeviceInfo.TYPE_BLE_HEADSET:
                case AudioDeviceInfo.TYPE_BLE_SPEAKER:
                case AudioDeviceInfo.TYPE_USB_HEADSET:
                case AudioDeviceInfo.TYPE_USB_DEVICE:
                    return false;
                default:
                    break;
            }
        }
        return true;
    }
}
