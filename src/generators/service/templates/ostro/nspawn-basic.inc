Type=notify
ExecStartPre=/bin/mkdir -p {C}/dev/../sys/../proc/../etc/../usr
ExecStartPre=/bin/mkdir -p {C}/var/../run/../tmp/../home/../root
{FW-OPEN}({data.manifest.container})
ExecStart=/usr/bin/systemd-nspawn \
    --keep-unit \
.   {if} ({data.manifest.container.type} == 'nspawn-shared') {then}
....    --share-system \
.   {end}
    --as-pid2 \
    -M {U} \
    -D {C} \
    --user={U} \
    {OVERLAY-RW}('/etc') \
    {OVERLAY-RW}('/var') \
    {OVERLAY-RO}('/lib') \
    {OVERLAY-RO}('/sbin') \
    {OVERLAY-RO}('/bin') \
    {OVERLAY-RO}('/usr') \
    {TMPFS}('/tmp', '0755') \
    {NETWORK}({data.manifest.container}) \
    {COMMAND}({data.manifest.command})

{FW-CLOSE}({data.manifest.container})
