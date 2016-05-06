{REQUEST-ADDON}('name:self/system-tmpfs', 'destination:/run/tmpfiles.d/tmpfs.conf')
{REQUEST-ADDON}('name:self/container-preset', 'destination:/run/systemd/system-preset/container.preset')

service:--preset-mode=full preset-all
exit:0
