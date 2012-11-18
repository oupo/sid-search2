function loadBinaryFile(url, fn) {
	var xhr = new XMLHttpRequest;
	xhr.open("GET", url);
	if (xhr.overrideMimeType) {
		xhr.overrideMimeType("text/plain; charset=x-user-defined");
	} else {
		xhr.setRequestHeader("Accept-Charset", "x-user-defined");
	}
	xhr.onreadystatechange = function() {
		if (xhr.readyState === 4) {
			var string = (typeof xhr.responseBody === "unknown") ? GetIEByteArray_ByteStr(xhr.responseBody) : xhr.responseText;
			var array = stringToByteArray(string);
			fn(xhr, array);
		}
	};
	xhr.send(null);
}

function stringToByteArray(s) {
	var array = [];
	for (var i = 0; i < s.length; i ++) {
		array[i] = s.charCodeAt(i) & 0xff;
	}
	return array;
}

//this snippet is from http://miskun.com/javascript/internet-explorer-and-binary-files-data-access/
function GetIEByteArray_ByteStr(IEByteArray) {
	var ByteMapping = {};
	for ( var i = 0; i < 256; i++ ) {
		for ( var j = 0; j < 256; j++ ) {
			ByteMapping[ String.fromCharCode( i + j * 256 ) ] = 
				String.fromCharCode(i) + String.fromCharCode(j);
		}
	}
	var rawBytes = IEBinaryToArray_ByteStr(IEByteArray);
	var lastChr = IEBinaryToArray_ByteStr_Last(IEByteArray);
	return rawBytes.replace(/[\s\S]/g, 
		function( match ) { return ByteMapping[match]; }) + lastChr;
}
 
var IEBinaryToArray_ByteStr_Script = 
	"<!-- IEBinaryToArray_ByteStr -->\r\n"+
	"<script type='text/vbscript'>\r\n"+
	"Function IEBinaryToArray_ByteStr(Binary)\r\n"+
	"	IEBinaryToArray_ByteStr = CStr(Binary)\r\n"+
	"End Function\r\n"+
	"Function IEBinaryToArray_ByteStr_Last(Binary)\r\n"+
	"	Dim lastIndex\r\n"+
	"	lastIndex = LenB(Binary)\r\n"+
	"	if lastIndex mod 2 Then\r\n"+
	"		IEBinaryToArray_ByteStr_Last = Chr( AscB( MidB( Binary, lastIndex, 1 ) ) )\r\n"+
	"	Else\r\n"+
	"		IEBinaryToArray_ByteStr_Last = "+'""'+"\r\n"+
	"	End If\r\n"+
	"End Function\r\n"+
	"</script>\r\n";

document.write(IEBinaryToArray_ByteStr_Script);

