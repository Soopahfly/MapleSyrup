param([string]$BinFile="bt2maple.bin",[string]$OutFile="bt2maple.uf2")
$FAMILY_ID=[System.Convert]::ToUInt32("E48BFF59",16)
$START_ADDR=[System.Convert]::ToUInt32("10000000",16)
$FLAGS=[System.Convert]::ToUInt32("00002000",16)
$PAYLOAD=[uint32]256
$MAGIC0=[System.Convert]::ToUInt32("0A324655",16)
$MAGIC1=[System.Convert]::ToUInt32("9E5D5157",16)
$MAGIC_END=[System.Convert]::ToUInt32("0AB16F30",16)
$bin=[System.IO.File]::ReadAllBytes($BinFile)
$binLen=$bin.Length
$padded=$binLen+(256-($binLen%256))%256
$data=New-Object byte[] $padded
[Array]::Copy($bin,$data,$binLen)
$totalBlocks=$padded/256
$out=New-Object System.IO.FileStream($OutFile,[System.IO.FileMode]::Create)
$bw=New-Object System.IO.BinaryWriter($out)
for($i=0;$i-lt$totalBlocks;$i++){
$addr=$START_ADDR+$i*$PAYLOAD
$bw.Write($MAGIC0);$bw.Write($MAGIC1);$bw.Write($FLAGS);$bw.Write($addr)
$bw.Write($PAYLOAD);$bw.Write([uint32]$i);$bw.Write([uint32]$totalBlocks);$bw.Write($FAMILY_ID)
$bw.Write($data,$i*$PAYLOAD,$PAYLOAD)
$bw.Write([byte[]]::new(220))
$bw.Write($MAGIC_END)}
$bw.Close();$out.Close()
Write-Host "Written: $OutFile ($($totalBlocks*512) bytes, $totalBlocks blocks)"
