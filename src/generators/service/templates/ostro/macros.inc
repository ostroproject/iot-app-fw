{macro} U   {do}{data.provider}-{data.manifest.application}{end}
{macro} C   {do}{data.path.container}/{U}-root{end}
{macro} A   {do}/apps/{data.provider}/{data.manifest.application}{end}
{macro} CIF {do}
.               {truncate}({concat}('ve-', {data.provider},
.                                   '-', {data.manifest.application}), 15)
{end}
{macro} IPT {do}/usr/sbin/iptables{end}

{macro} OVERLAY-RW(path)  {do}--overlay={path}:{A}{path}:{path}{end}
{macro} OVERLAY-RO(path)  {do}--overlay-ro={path}:{A}{path}:{path}{end}
{macro} TMPFS(path, mode) {do}--tmpfs={path}:mode={mode}{end}

{macro} ENVIRONMENT {do}
.   {foreach} name:value {in}{data.manifest.environment} {do}
.       Environment={name}={value}
.   {end}
{end}

{macro} GROUPS {do}
.   {if} ({data.manifest.groups}) {then}
.       SupplementaryGroups={foreach}:g{in} {data.manifest.groups} {do}{g} {end}
.   {end}
{end}

{macro} NETWORK(container) {do}
.   {if} ({container.network} == 'VirtualEthernet') {then}
........--private-network --network-veth \
........{if} ({container.type} != 'nspawn-system') {then}
............--auto-dhcp \
........{end}
.   {end}
    {foreach} e {in} {container.portmap} {do} -p {e.proto}:{e.host}:{e.container} {end}
{end}

{macro} FW-OPEN(container) {do}
.    ExecStartPre={IPT} -t filter -I FORWARD -o {CIF} -j ACCEPT
.    {foreach} e {in} {container.portmap} {do}
..........ExecStartPre={IPT} -t filter -I INPUT -p {e.proto} --dport {e.host} -j ACCEPT
.    {end}
{end}

{macro} FW-CLOSE(container) {do}
.    ExecStopPost={IPT} -t filter -D FORWARD -o {CIF} -j ACCEPT
.    {foreach} e {in} {container.portmap} {do}
..........ExecStopPost={IPT} -t filter -D INPUT -p {e.proto} --dport {e.host} -j ACCEPT
.    {end}
{end}

{macro} COMMAND(command) {do}
.    {foreach} arg {in} {command} {do}{arg} {end}
{end}