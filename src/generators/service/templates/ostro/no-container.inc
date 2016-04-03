Type=simple
User={U}
{FW-OPEN}({data.manifest.container})
ExecStart={COMMAND}({data.manifest.command})
{FW-CLOSE}({data.manifest.container})
