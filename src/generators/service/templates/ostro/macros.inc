{macro} U   {do}{data.provider}-{data.manifest.application}{end}
{macro} C   {do}{data.path.container}/{U}-root{end}
{macro} A   {do}/apps/{data.provider}/{data.manifest.application}{end}
{macro} CIF {do}
.               {TRUNCATE}({CONCAT}('ve-', {data.provider},
.                                   '-', {data.manifest.application}), 15)
{end}
{macro} IPT {do}/usr/sbin/iptables -w{end}

{macro} OVERLAY-RW(path)  {do}--overlay={path}:{A}{path}:{path}{end}
{macro} OVERLAY-RO(path)  {do}--overlay-ro={path}:{A}{path}:{path}{end}
{macro} TMPFS(path, mode) {do}--tmpfs={path}:mode={mode}{end}

{macro} ENVIRONMENT(manifest) {do}
.   {foreach} name:value {in}{manifest.environment} {do}
.       Environment={name}={value}
.   {end}
.   {if} (!{manifest.container} || {manifest.container.type} == 'none') {then}
.       Environment=LD_LIBRARY_PATH={A}/lib:/lib:/usr/lib
.       Environment=PATH={A}/bin:{A}/sbin:/bin:/usr/bin:/sbin:/usr/sbin
.   {end}
{end}

{macro} GROUPS(manifest) {do}
.   {if} ({manifest.groups}) {then}
.       SupplementaryGroups={foreach}:g{in} {manifest.groups} {do}{g} {end}
.   {end}
{end}

{macro} NETWORK(container) {do}
.   {if} ({container.network} == 'VirtualEthernet') {then}
........--private-network --network-veth \
........{if} ({container.type} != 'nspawn-system') {then}
........    --auto-dhcp \
........{end}
.   {end}
    {foreach} e {in} {container.portmap} {do} -p {e.proto}:{e.host}:{e.container} {end}
{end}

{macro} FW-CHECK(container) {do}
.    {if} ({container.portmap} || {container.type} != 'none') {then}
.        Requires={data.provider}-{data.manifest.application}-firewall.service
.        After={data.provider}-{data.manifest.application}-firewall.service
.        {REQUEST-DROPIN}('firewall')
.    {end}
{end}

{macro} FW-OPEN(container) {do}
.    {if} ({container.type} != 'none') {then}
.        ExecStartPre={IPT} -t filter -I FORWARD -o {CIF} -j ACCEPT
.    {end}
.    {foreach} e {in} {container.portmap} {do}
..........ExecStartPre={IPT} -t filter -I INPUT -p {e.proto} --dport {e.host} -j ACCEPT
.    {end}
{end}

{macro} FW-CLOSE(container) {do}
.    {if} ({container.type} != 'none') {then}
.        ExecStopPost={IPT} -t filter -D FORWARD -o {CIF} -j ACCEPT
.    {end}
.    {foreach} e {in} {container.portmap} {do}
..........ExecStopPost={IPT} -t filter -D INPUT -p {e.proto} --dport {e.host} -j ACCEPT
.    {end}
{end}

{macro} COMMAND(command) {do}
.    {foreach} arg {in} {command} {do}{arg} {end}
{end}

{macro} AUTOSTART-CHECK(manifest) {do}
.    {if} ({manifest.autostart} == 'true' ||
           {manifest.autostart} == 'yes') {then}
.        {REQUEST-DROPIN}('autostart')
.    {end}
{end}
