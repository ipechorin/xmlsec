<xsl:stylesheet version = '1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>
    <xsl:output method="html" />
    <xsl:param name="topfolder"></xsl:param>

    <xsl:template name="linkto">
	<xsl:element name="a"> 
	    <xsl:attribute name="href">
		<xsl:value-of select="$topfolder"/>
		<xsl:value-of select="$href"/>
	    </xsl:attribute>		
	    <xsl:value-of select="$text"/>
	</xsl:element>
    </xsl:template>

    <xsl:template name="myimg">
	<xsl:element name="img"> 
	    <xsl:attribute name="src">
		<xsl:value-of select="$topfolder"/>
		<xsl:value-of select="$src"/>
	    </xsl:attribute>		
	    <xsl:attribute name="alt">
		<xsl:value-of select="$alt"/>
	    </xsl:attribute>			    
	    <xsl:attribute name="border">0</xsl:attribute>
	</xsl:element>
    </xsl:template>
    
    <xsl:template match="/">

<html>
<head>
<xsl:copy-of select="//head/*" />
</head>
<body>
<table witdh="100%"><tr>
    <!-- top navigation menu start -->     
    <td valign="top" align="left" width="210">
	<xsl:call-template name="myimg"> 
	    <xsl:with-param name="src">logo.gif</xsl:with-param> 
	    <xsl:with-param name="alt">XML Security Library</xsl:with-param> 
	</xsl:call-template>
        <p/>
        <ul>

	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">index.html</xsl:with-param> 
		    <xsl:with-param name="text">Home</xsl:with-param> 
		</xsl:call-template>
	    </li>	    
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">download.html</xsl:with-param> 
		    <xsl:with-param name="text">Download</xsl:with-param> 
		</xsl:call-template>
	    </li>
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">news.html</xsl:with-param> 
		    <xsl:with-param name="text">News</xsl:with-param> 
		</xsl:call-template>
	    </li>	    
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">documentation.html</xsl:with-param> 
		    <xsl:with-param name="text">Documentation</xsl:with-param> 
		</xsl:call-template>
	    </li>	    
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">faq.html</xsl:with-param> 
		    <xsl:with-param name="text">FAQ</xsl:with-param> 
		</xsl:call-template>
	    </li>	    
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">xmldsig.html</xsl:with-param> 
		    <xsl:with-param name="text">XML Digital Signature</xsl:with-param> 
		</xsl:call-template>
	    </li>	    
	    <ul>
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">xmldsig-interop.html</xsl:with-param> 
		    <xsl:with-param name="text">Interop report</xsl:with-param> 
		</xsl:call-template>
	    </li>	    
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">xmldsig-verifier.html</xsl:with-param> 
		    <xsl:with-param name="text">Online Verifier</xsl:with-param> 
		</xsl:call-template>
	    </li>
	    </ul>
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">xmlenc.html</xsl:with-param> 
		    <xsl:with-param name="text">XML Encryption</xsl:with-param> 
		</xsl:call-template>
	    </li>
	    <ul>
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">xmlenc-interop.html</xsl:with-param> 
		    <xsl:with-param name="text">Interop report</xsl:with-param> 
		</xsl:call-template>
	    </li>	
	    </ul>    
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">c14n.html</xsl:with-param> 
		    <xsl:with-param name="text">XML Canonicalization</xsl:with-param> 
		</xsl:call-template>
	    </li>
	    <li>
		<xsl:call-template name="linkto"> 
		    <xsl:with-param name="href">bugs.html</xsl:with-param> 
		    <xsl:with-param name="text">Reporting Bugs</xsl:with-param> 
		</xsl:call-template>
	    </li>
            <li><a href="http://www.aleksey.com/pipermail/xmlsec">Mailing list</a></li>
	</ul>
      	<a href="http://xmlsoft.org/">
	    <xsl:call-template name="myimg"> 
		<xsl:with-param name="src">libxml2-logo.png</xsl:with-param> 
		<xsl:with-param name="alt">LibXML2</xsl:with-param> 
	    </xsl:call-template>
	</a><br/>
      	<a href="http://xmlsoft.org/XSLT">
	    <xsl:call-template name="myimg"> 
		<xsl:with-param name="src">libxslt-logo.png</xsl:with-param> 
		<xsl:with-param name="alt">LibXSLT</xsl:with-param> 
	    </xsl:call-template>
	</a><br/>
      	<a href="http://www.openssl.org/">
	    <xsl:call-template name="myimg"> 
		<xsl:with-param name="src">openssl-logo.png</xsl:with-param> 
		<xsl:with-param name="alt">OpenSSL</xsl:with-param> 
	    </xsl:call-template>
	</a><br/>
     </td>
     <!-- top navigation menu end -->

     <td valign="top" align="left" > 
        <table width="100%">
	    <tr><td valign="top" align="left" id="xmlsecContent" > 
	    <xsl:choose> 
		<xsl:when test="//td[@id='xmlsecContent']">
		    <xsl:copy-of select="//td[@id='xmlsecContent']/*" />
		</xsl:when>
		<xsl:otherwise>
		    <xsl:copy-of select="//body/*" />
		</xsl:otherwise>
	    </xsl:choose>
	    </td></tr>
	</table>
	<br/><br/>
	<p><a href="/bugs.html">Aleksey Sanin</a></p>
    </td>


</tr></table>
</body>
</html>

    </xsl:template>
</xsl:stylesheet>
