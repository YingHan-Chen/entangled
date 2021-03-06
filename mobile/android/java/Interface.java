package org.iota.mobile;

public class Interface {
    public static native String iota_pow_trytes(String trytes, int mwm);
    public static native String[] iota_pow_bundle(String[] bundle, String trunk, String branch, int mwm);
    public static native String iota_sign_address_gen_trytes(String seed, int index, int security);
    public static native byte[] iota_sign_address_gen_trits(byte[] seed, int index, int security);
    public static native String iota_sign_signature_gen_trytes(String seed, int index, int security, String bundleHash);
    public static native byte[] iota_sign_signature_gen_trits(byte[] seed, int index, int security, byte[] bundleHash);
    public static native String iota_digest(String trytes);
    public static native long bundle_miner_mine(byte[] bundle_normalized_max, int security, byte[] essence, int essence_length, int count, int nprocs, int mining_threshold);
}
