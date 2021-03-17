find_emmc_part() {
        local partname="$1"
        filename=$(grep -lr "$partname" /sys/block/mmcblk*/)
        devname=$(grep DEVNAME $filename)
        dev=${devname#*=}
        echo $dev
}

emmc_get_mac_ascii() {
        local partname="$1"
        local key="$2"
        local part
        local mac_dirty

        part=$(find_emmc_part "$partname")
        if [ -z "/dev/$part" ]; then
                echo "emmc_get_mac_ascii: partition $partname not found!" >&2
                return
        fi

        mac_dirty=$(strings "/dev/$part" | sed -n 's/^'"$key"'=//p')

        # "canonicalize" mac
        [ -n "$mac_dirty" ] && macaddr_canonicalize "$mac_dirty"
}
