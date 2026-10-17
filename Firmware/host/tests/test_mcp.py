"""Exercise the executable with the official MCP client. --hardware enables real USB calls."""
import argparse
import asyncio
import json
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[2]

async def run(hardware=False, flash=False):
    executable = ROOT/'host/build/Coral-Wave-CLI.exe'
    firmware = ROOT/'releases/Coral-Wave.cwfw'
    checks = []
    async with stdio_client(StdioServerParameters(command=str(executable), args=['--mcp'])) as streams:
        async with ClientSession(*streams) as session:
            initialized = await session.initialize()
            assert initialized.serverInfo.name == 'coral-wave'
            checks.append('official MCP SDK initialize and ping')
            await session.send_ping()
            tools = (await session.list_tools()).tools
            names = {t.name: t for t in tools}
            assert len(names) == 10 and names['coral_flash_firmware'].annotations.destructiveHint
            assert names['coral_get_audio'].annotations.readOnlyHint
            checks.append('ten tools with schemas and mutation annotations')
            async def call(name, args=None, error=False):
                result = await session.call_tool(name, args or {})
                assert bool(result.isError) == error, result
                if error:
                    return result
                assert result.structuredContent is not None
                assert json.loads(result.content[0].text) == result.structuredContent
                return result.structuredContent
            result = await call('coral_validate_firmware', {'path': str(firmware)})
            assert result['valid'] and result['version'] == '3.0.0'
            await call('coral_validate_firmware', {'path': 'relative.cwfw'}, error=True)
            await call('coral_not_a_tool', error=True)
            checks.append('firmware validation, bad path and unknown tool errors')
            devices = await call('coral_list_devices')
            if hardware:
                assert len(devices['devices']) == 1, devices
                serial = devices['devices'][0]['serial']
                args = {'serial': serial}
                status = await call('coral_get_status', args)
                assert status['status']['wm8978_i2c_ack'] and status['status']['error'] == 0
                original = await call('coral_get_audio', args)
                patch = {'eq_gain_db': [-2, 1, 2, 1, 0], 'dac_preamp_db': -6,
                         'limiter': True, 'limiter_threshold_dbfs': -3,
                         'high_pass': 'speech', 'high_pass_hz': 153}
                try:
                    applied = await call('coral_set_audio', {**args, 'patch': patch})
                    assert all(applied[k] == v for k, v in patch.items())
                    # The automatic sample-rate format is not an exposed writable field.
                    await call('coral_set_audio', {**args, 'patch': {'sample_rate': 192000}}, error=True)
                    await call('coral_set_audio', {**args, 'patch': {'eq_gain_db': [13, 0, 0, 0, 0]}}, error=True)
                    await call('coral_set_audio', {**args, 'patch': {'microphone_pga_db': 18.1}}, error=True)
                    unchanged = await call('coral_get_audio', args)
                    assert unchanged == applied
                finally:
                    writable = names['coral_set_audio'].inputSchema['properties']['patch']['properties']
                    restore = {k: original[k] for k in writable if k in original}
                    # In audio/off mode the reported cutoff is 3.7/0 Hz, not a speech cutoff option.
                    if restore.get('high_pass') != 'speech':
                        restore.pop('high_pass_hz', None)
                    await call('coral_set_audio', {**args, 'patch': restore})
                checks.append('real USB status, physical-unit audio patch, reject invalid writes, restore settings')
                if flash:
                    progress = []
                    async def update_progress(value, total, message):
                        progress.append(value)
                    result = await session.call_tool('coral_flash_firmware', {**args, 'path': str(firmware)}, progress_callback=update_progress)
                    assert not result.isError and result.structuredContent['flashed']
                    assert progress and progress[-1] == 100 and all(b > a for a, b in zip(progress, progress[1:]))
                    after = await call('coral_get_status', args)
                    assert after['firmware'] == '3.0.0' and after['status']['wm8978_i2c_ack']
                    checks.append('real MCP-triggered USB flash, increasing progress, CRC and reboot verification')
    print(json.dumps({'passed': True, 'checks': checks}, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hardware', action='store_true')
    parser.add_argument('--flash', action='store_true')
    args = parser.parse_args()
    asyncio.run(run(args.hardware, args.flash))
