{if} ({CONTAINER.network.type} == 'VirtualEthernet') {then}
.    exec:/usr/sbin/dhcpcd -i host0
{end}
setuser:{USER}
{if} ({SERVICE.groups}) {then}
.    setgroups:{foreach} g {in} {SERVICE.groups} {do}
.       {!first}g{do},{end}{g}{\}
.    {end}{\n}
{end}
{foreach} cmd {in} {SERVICE.start} {do}
.    exec:{foreach} arg {in} {cmd} {do}{arg} {end}{\n}
{end}