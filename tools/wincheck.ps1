<#
    What a host sees of a built Windows DLL, with no GL and no Resolume.

        tools/wincheck.ps1 -Dll build/Release/Pepper.dll -Name "SW Pepper" -Id PG01 `
                           -Kind mixer -Params 31 -First "Linear Light"
        tools/wincheck.ps1 -Dll build/Release/PepperFeed.dll -Name "SW Pepper Feed" `
                           -Id PG02 -Kind effect -Params 8 -First "Sides"

    It LOADS the DLL (so a missing runtime or dependency fails here, where
    dumpbin cannot see it), finds plugMain, and asks it what Resolume asks
    first: FF_GET_INFO (the name, read back as the char[16] a host reads, the
    id and the type) and the parameters (their count and names) -- which the
    SDK answers from a prototype instance, so the constructor runs too.

    Throw's, with a mixer kind (FF_GET_INFO type 2) and the first parameter's
    name as an argument, since the two plugins start differently. For the
    mixer it also asks FF_GET_PLUGIN_CAPS for the input counts: a mixer's two
    inputs are a separate declaration from its type, and nothing else on this
    runner would notice them missing. Run it on a DLL built on Windows; the
    pixels are checked on a Mac (tools/verify.sh).
#>
param(
    [Parameter( Mandatory )] [string] $Dll,
    [Parameter( Mandatory )] [string] $Name,
    [Parameter( Mandatory )] [string] $Id,
    [Parameter( Mandatory )] [ValidateSet( 'source', 'effect', 'mixer' )] [string] $Kind,
    [Parameter( Mandatory )] [int] $Params,
    [Parameter( Mandatory )] [string] $First
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Ffgl
{
    [DllImport( "kernel32", SetLastError = true, CharSet = CharSet.Unicode )]
    public static extern IntPtr LoadLibraryW( string path );
    [DllImport( "kernel32", SetLastError = true, CharSet = CharSet.Ansi )]
    public static extern IntPtr GetProcAddress( IntPtr module, string name );
    // FFMixed is a union of a 32-bit integer and a pointer: pointer-sized.
    [UnmanagedFunctionPointer( CallingConvention.StdCall )]
    public delegate IntPtr PlugMain( uint code, IntPtr input, IntPtr instance );
}
'@

$failures = 0
function Check( [bool] $ok, [string] $what )
{
    if( $ok ) { Write-Host "  ok    $what" }
    else { Write-Host "  FAIL  $what"; $script:failures++ }
}

$path = ( Resolve-Path $Dll ).Path
Write-Host "$path"
$module = [Ffgl]::LoadLibraryW( $path )
if( $module -eq [IntPtr]::Zero )
{
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "LoadLibrary failed (Win32 error $err): a missing dependency or runtime"
}
$proc = [Ffgl]::GetProcAddress( $module, 'plugMain' )
Check ( $proc -ne [IntPtr]::Zero ) 'loads, and exports plugMain'
if( $proc -eq [IntPtr]::Zero ) { exit 1 }
$plugMain = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer( $proc, [Ffgl+PlugMain] )

# FF_GET_INFO (0): PluginInfoStruct -- two FFUInt32 API versions, char[4]
# id, char[16] name (neither null-terminated), FFUInt32 type.
$info  = $plugMain.Invoke( 0, [IntPtr]::Zero, [IntPtr]::Zero )
$bytes = New-Object byte[] 32
[Runtime.InteropServices.Marshal]::Copy( $info, $bytes, 0, 32 )
$gotId   = [Text.Encoding]::ASCII.GetString( $bytes, 8, 4 )
$gotName = [Text.Encoding]::ASCII.GetString( $bytes, 12, 16 ).TrimEnd( [char]0 )
$gotType = [BitConverter]::ToUInt32( $bytes, 28 )
$wantType = switch( $Kind ) { 'effect' { 0 } 'source' { 1 } 'mixer' { 2 } }
Check ( $gotId -eq $Id ) "id $gotId (expected $Id)"
Check ( $gotName -eq $Name ) "name '$gotName', as a host reads char[16] (expected '$Name')"
Check ( $gotType -eq $wantType ) "type $gotType, $Kind"

# FF_GET_NUM_PARAMETERS (4) and FF_GET_PARAMETER_NAME (5): the SDK builds a
# prototype instance to answer, so the plugin's constructor runs here. The
# count is the low 32 bits of an FFMixed, as a host reads it: the high half of
# the register is whatever the plugin left there (333 on windows-latest's
# MSVC). 0xffffffffL, not 0xffffffff, which PowerShell reads as an Int32 -1
# and so masks nothing.
$count = [int]( $plugMain.Invoke( 4, [IntPtr]::Zero, [IntPtr]::Zero ).ToInt64() -band 0xffffffffL )
$names = @()
for( $i = 0; $i -lt $count; $i++ )
{
    $p = $plugMain.Invoke( 5, [IntPtr]$i, [IntPtr]::Zero )
    $names += [Runtime.InteropServices.Marshal]::PtrToStringAnsi( $p )
}
Check ( $count -eq $Params ) "$count parameters (expected $Params): $( $names[0] ) .. $( $names[-1] )"
Check ( $names[0] -eq $First -and $names -contains 'About' ) "$First first; About among them"
if( $Kind -eq 'mixer' )
{
    # Arena binds a mixer parameter named Opacity to the layer fader.
    Check ( $names -contains 'Opacity' ) 'Opacity among them (the layer fader)'
    # FF_GET_PLUGIN_CAPS (10): caps 10 and 11 are the minimum and maximum
    # input counts, answered in the low 32 bits.
    $min = [int]( $plugMain.Invoke( 10, [IntPtr]10, [IntPtr]::Zero ).ToInt64() -band 0xffffffffL )
    $max = [int]( $plugMain.Invoke( 10, [IntPtr]11, [IntPtr]::Zero ).ToInt64() -band 0xffffffffL )
    Check ( $min -eq 2 -and $max -eq 2 ) "inputs $min to $max (expected 2 to 2)"
}

if( $failures -gt 0 ) { Write-Host "`n  FAIL ($failures)"; exit 1 }
Write-Host "`n  PASS"
