{if} ({CONTAINER.network.type} == 'VirtualEthernet') {then}
.    exec:/usr/sbin/dhcpcd -i host0
{end}
setuser:{USER}
{if} ({SERVICE.groups}) {then}
.    setgroups:{USER}{foreach} g {in} {SERVICE.groups} {do},{g}{end}{\n}
{else}
.    setgroups:{USER}
{end}
{foreach} cmd {in} {SERVICE.start} {do}
.    exec:{foreach} arg {in} {cmd} {do}{arg} {end}{\n}
{end}