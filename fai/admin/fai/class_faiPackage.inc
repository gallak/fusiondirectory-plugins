<?php
/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2003-2010  Cajus Pollmeier
  Copyright (C) 2011-2016  FusionDirectory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

class PackageSelectDialog extends GenericSelectDialog
{
  protected $dialogClass = 'packageSelect';

  function handle_finish ()
  {
    $result = $this->dialog->detectPostActions();
    if (isset($result['targets'])) {
      $headpage = $this->dialog->getHeadpage();
      foreach ($result['targets'] as $dn) {
        $attrs = $headpage->getEntry($dn);

        $this->attribute->addValue($attrs['PACKAGE'][0]);
      }
    }
    $this->attribute->genPkgs();
    return FALSE;
  }
}

class PackageConfigDialog extends GenericDialog
{
  protected $dialogClass = 'faiPackageConfiguration';

  protected $post_cancel = 'CancelObjectConfig';
  protected $post_finish = 'SaveObjectConfig';

  function __construct($simplePlugin, &$attribute, $key, $pkg_config)
  {
    $this->attribute  = $attribute;
    $this->dialog     = new $this->dialogClass($simplePlugin->dn, $key, $simplePlugin->FAIdebianRelease, $pkg_config);
  }

  function execute ()
  {
    if (isset($_POST[$this->post_cancel])) {
      return $this->handle_cancel();
    }
    if (isset($_POST[$this->post_finish]) || isset($_GET[$this->post_finish])) {
      return $this->handle_finish();
    }
    return $this->dialog_execute();
  }

  function dialog_execute ()
  {
    $this->dialog->save_object();
    return $this->dialog->execute();
  }

  function handle_finish ()
  {
    $this->dialog->save_object();

    $msgs = $this->dialog->check();
    if (count($msgs)) {
      msg_dialog::displayChecks($msgs);
      return $this->dialog->execute();
    }

    $this->attribute->packageConfigured($this->dialog->save());
    return FALSE;
  }

  function handle_cancel ()
  {
    return FALSE;
  }
}

class PackagesAttribute extends DialogOrderedArrayAttribute
{
  protected $dialogClass        = 'PackageSelectDialog';
  protected $buffer             = array();
  protected $configuredPackages = array();

  function __construct ($label, $description, $ldapName, $required = FALSE, $defaultValue = array(), $acl = "")
  {
    OrderedArrayAttribute::__construct(
      new StringAttribute($label, $description, $ldapName, $required, "", $acl),
      FALSE,
      $defaultValue,
      FALSE
    );
  }

  function addValue($value)
  {
    $this->value[preg_replace('/\-$/', '', $value)] = $value;
  }

  function readValue($value)
  {
    return array(preg_replace('/\-$/', '', $value), $value);
  }

  function getFilterBlackList ()
  {
    return array('PACKAGE' => array_keys($this->value));
  }

  function renderButtons ()
  {
    $id = $this->getHtmlId();
    $buttons  = '<input type="submit" name="add'.$id.'" id="add'.$id.'"'.
                ($this->disabled ? ' disabled="disabled"' : '').
                ' value="{msgPool type=addButton}"/>';
    $buttons  .= '<input type="submit" name="add'.$id.'_dialog" id="add'.$id.'_dialog"'.
                ($this->disabled ? ' disabled="disabled"' : '').
                ' value="{msgPool type=addButton}{t} from list{/t}"/>';
    return $buttons;
  }

  public function htmlIds()
  {
    $id = $this->getHtmlId();
    return array_merge(array('add'.$id), parent::htmlIds());
  }

  protected function getAttributeArrayValue($key, $value)
  {
    if (!isset($this->buffer[$value])) {
      $this->genPkgs();
    }
    if (isset($this->buffer[$value])) {
      return array(
        $this->buffer[$value]['PACKAGE'],
        $this->buffer[$value]['VERSION'],
        base64_decode($this->buffer[$value]['DESCRIPTION']),
      );
    }
    return array($value, '', '');
  }

  protected function genRowIcons($key, $value)
  {
    $id = $this->getHtmlId();
    list ($img, $nbicons) = parent::genRowIcons($key, $value);
    $key64 = base64_encode($key);
    if (preg_match('/\-$/', $value)) {
      $img = '<input type="image" src="geticon.php?context=actions&icon=add&size=16"'.
              ' name="'.$id.'_removal_'.$key64.'" class="center" alt="'._('Mark for installation').'" title="'._('Mark for installation').'"/>'.
              $img;
    } else {
      $img = '<input type="image" src="geticon.php?context=actions&icon=remove&size=16"'.
              ' name="'.$id.'_removal_'.$key64.'" class="center" alt="'._('Mark for removal').'" title="'._('Mark for removal').'"/>'.
              $img;
    }
    if (isset($this->buffer[$key]['HASTEMPLATE']) && $this->buffer[$key]['HASTEMPLATE']) {
      $img = '<input type="image" src="geticon.php?context=actions&icon=package-configure&size=16"'.
              ' name="'.$id.'_config_'.$key64.'" class="center" alt="'._('Edit configuration').'" title="'._('Edit configuration').'"/>'.
              $img;
    } else {
      $img = '<img src="images/empty.png" alt="" style="width:16px;"/>'.$img;
    }
    $nbicons += 2;

    return array ($img, $nbicons);
  }

  protected function handleAddAndEditValue()
  {
    $id = $this->getHtmlId();

    foreach (array_keys($_POST) as $name) {
      if (preg_match('/^'.$id.'_config_/', $name)) {
        $key = preg_replace('/^'.$id.'_config_/', '', $name);
        $key = base64_decode(preg_replace('/_[xy]$/', '', $key));
        /* Open configuration dialog */
        $pkg_config = array();
        if (isset($this->configuredPackages[$key])) {
          $pkg_config = $this->configuredPackages[$key];
        }
        $this->plugin->openDialog(
          new PackageConfigDialog($this->plugin, $this, $key, $pkg_config)
        );
        break;
      } elseif (preg_match('/^'.$id.'_removal_/', $name)) {
        $key = preg_replace('/^'.$id.'_removal_/', '', $name);
        $key = base64_decode(preg_replace('/_[xy]$/', '', $key));
        if (preg_match('/\-$/', $this->postValue[$key])) {
          $this->postValue[$key] = preg_replace('/\-$/', '', $this->postValue[$key]);
        } else {
          $this->postValue[$key] .= '-';
        }
        break;
      }
    }

    parent::handleAddAndEditValue();
  }

  function packageConfigured($packageConfig)
  {
    $this->configuredPackages = array_merge($this->configuredPackages, $packageConfig);
  }

  /* Special LDAP treatment that this attribute does after plugin ldap save */
  function postLdapSave ($ldap)
  {
    /* First delete all old nodes */
    $ldap->cd($this->plugin->dn);
    $ldap->search('objectClass=FAIdebconfInfo', array('dn'), 'one');
    $delete = array();
    while ($attrs = $ldap->fetch()) {
      $delete[] = $attrs['dn'];
    }
    foreach ($delete as $dn) {
      $ldap->rmdir($dn);
    }

    /* Then add our values */
    foreach ($this->configuredPackages as $pkgname => $pkgvars) {
      foreach ($pkgvars as $varname => $varinfos) {
        $attrs = array('objectClass' => 'FAIdebconfInfo');
        $attrs['FAIpackage']           = $pkgname;
        $attrs['FAIvariable']          = $varname;
        $attrs['FAIvariableType']      = $varinfos['Type'];
        $attrs['FAIvariableContent']   = $varinfos['Value'];
        $vardn = 'FAIvariable='.$varname.','.$this->plugin->dn;

        $ldap->cd($vardn);
        foreach (array_keys($attrs) as $index) {
          if (is_array($attrs[$index]) && (count($attrs[$index]) == 0)) {
            unset($attrs[$index]);
          }
        }
        $ldap->add($attrs);
        if (!$ldap->success()) {
          msg_dialog::display(_('LDAP error'), msgPool::ldaperror($ldap->get_error(), $vardn, LDAP_ADD, get_class()), LDAP_ERROR);
        }
      }
    }
  }

  /*! \brief  Reload the list of cached packages.
      @return Returns the currently cached list of packages.
   */
  function genPkgs()
  {
    if (empty($this->plugin->FAIdebianRelease)) {
      return;
    }

    if (empty($this->value)) {
      // If there are no packages, no need to do anything
      return;
    }

    $attrs = array('distribution','package','version','section','description','timestamp','hastemplate');

    // packages names that are not already in the buffer
    $packages = array_diff_key($this->value, $this->buffer);

    $release  = $this->plugin->FAIdebianRelease;
    $q        = new supportDaemon();
    $customs  = serviceRepository::getCustomReleases($release);
    for ($i = -1; (count($packages) > 0) && ($i < count($customs)); $i++) {
      // if release has been handled
      if ($i >= 0) {
        // take the next custom
        $release = $customs[$i];
      }
      $ret = $q->FAI_get_packages($release, $attrs, array_keys($packages), -1, -1, TRUE);
      if ($q->is_error()) {
        msg_dialog::display(_("Infrastructure service"), msgPool::siError($q->get_error()), ERROR_DIALOG);
        return $this->buffer;
      } else {
        foreach ($ret as $attr) {
          if (!isset($attr['DESCRIPTION'])) {
            $attr['DESCRIPTION'] = '';
          }
          $this->buffer[$attr['PACKAGE']] = $attr;
          // remove found packages
          unset($packages[$attr['PACKAGE']]);
          if ($i >= 0) {
            $this->FAIcustomRelease[$release] = $release;
          }
        }
      }
      @DEBUG (DEBUG_TRACE, __LINE__, __FUNCTION__, __FILE__, count($packages), "$release done, packages left");
    }

    if (count($packages) > 0) {
      msg_dialog::display(_("Unkown packages"), "The following packages were not found on the repository : ".join(",", $packages), ERROR_DIALOG);
      foreach ($packages as $p => $pp) {
        unset($this->value[$p]);
      }
    }

    $this->genConfiguredPackages();
  }

  function genConfiguredPackages()
  {
    global $config;
    /* Fetch all package configurations from ldap */
    $PackageFilter = "";
    foreach ($this->value as $name) {
      $PackageFilter .= "(FAIpackage=".$name.")";
    }
    /* Also fetch d-i */
    $PackageFilter .= "(FAIpackage=d-i)";

    $PackageFilter = "(&(objectClass=FAIdebconfInfo)(|".$PackageFilter."))";

    /* Search for configuration objects */
    $ldap = $config->get_ldap_link();
    $ldap->cd($this->plugin->dn);
    $ldap->search($PackageFilter, array("FAIvariable","FAIvariableType",
          "FAIvariableContent","FAIpackage","FAIdebianSection","FAIstate"));

    $this->configuredPackages = array();

    /* Walk through configurations and append them to our list of ConfiguredPackages */
    while ($attr = $ldap->fetch()) {
      /* Skip objects, that are tagged as removed */
      if (isset($attr['FAIstate'][0])) {
        if (preg_match("/removed$/", $attr['FAIstate'][0])) {
          continue;
        }
      }

      $tmp = array();
      $tmp['Name']  = $attr['FAIvariable'][0];
      $tmp['Type']  = $attr['FAIvariableType'][0];
      $tmp['Save']  = TRUE;

      if (isset($attr['FAIvariableContent'][0])) {
        if (!in_array($attr['FAIvariableType'], array("multiselect"))) {
          $tmp['Value'] = $attr['FAIvariableContent'][0];
        } else {
          $tmp['Value'] = array();
          unset($attr['FAIvariableContent']['count']);
          foreach ($attr['FAIvariableContent'] as $attr) {
            $tmp['Value'][] = $attr;
          }
        }
        $this->configuredPackages[$attr['FAIpackage'][0]][$attr['FAIvariable'][0]] = $tmp;
      } else {
        $tmp['Value']                                                               = "";
        $this->configuredPackages[$attr['FAIpackage'][0]][$attr['FAIvariable'][0]]  = $tmp;
      }
    }
  }
}

class faiPackage extends faiSimplePluginClass
{
  var $objectclasses = array('top','FAIclass','FAIpackageList','FAIrepository');

  static function plInfo()
  {
    return array(
      'plShortName'   => _('Package'),
      'plDescription' => _('FAI Package list'),
      'plObjectType'  => array(
        'faiPackage' => array(
          'name'        => _('FAI Package'),
          'filter'      => 'objectClass=FAIpackageList',
          'aclCategory' => 'fai',
          'ou'          => get_ou('faiPackageRDN'),
          'icon'        => 'geticon.php?context=applications&icon=fai-packages&size=16'
        )
      ),

      'plProvidedAcls' => parent::generatePlProvidedAcls(static::getAttributesInfo())
    );
  }

  static function getAttributesInfo ()
  {
    return array(
      'main' => array(
        'name'  => _('Properties'),
        'attrs' => array(
          new HostNameAttribute (
            _('Class name'), _('Variables class name'),
            'cn', TRUE
          ),
          new StringAttribute (
            _('Description'), _('Short description of the class'),
            'description', FALSE
          ),
        )
      ),
      'repo' => array(
        'name'  => _('Repository'),
        'attrs' => array(
          new DisplayLDAPAttribute (
            _('Release'), _('Debian release concerned'),
            'FAIdebianRelease', FALSE
          ),
          new DisplayLDAPArrayAttribute (
            _('Sections'), _('Sections concerned'),
            'FAIdebianSection', FALSE
          ),
          new SelectAttribute (
            _('Install method'), _('Install method to use for this package list'),
            'FAIinstallMethod', TRUE,
            array('install', 'ninstall', 'remove',
              'dselect-upgrade', 'taskinst', 'taskrm',
              'hold', 'clean', 'aptitude', 'aptitude-r',
              'pending', 'dpkgc', 'yumi'),
            'aptitude'
          ),
        )
      ),
      'packages' => array(
        'name'  => _('Packages'),
        'class' => array('fullwidth'),
        'attrs' => array(
          new PackagesAttribute (
            '', _('Packages in this class'),
            'FAIpackage', TRUE
          )
        )
      )
    );
  }

  function __construct ($dn = NULL, $object = NULL, $parent = NULL, $mainTab = FALSE)
  {
    parent::__construct($dn, $object, $parent, $mainTab);

    $this->attributesAccess['cn']->setUnique(TRUE);

    if ($this->FAIdebianRelease === '') {
      /* Assemble release name */
      $this->updateRelease();
    }

    session::set('packageSelect_Release', $this->FAIdebianRelease);
  }

  function updateRelease()
  {
    /* Assemble release name */
    $tmp = preg_replace('/'.preg_quote(get_ou('faiBaseRDN'), '/').'.*$/i', '', $this->base);
    preg_match_all('/ou=([^=,]+),/', $tmp, $m);
    /* m[1][0] is release and m[1][1] is distribution */
    $this->FAIdebianRelease = $m[1][0];

    /* Assemble sections */
    $repos = $this->getServerInfos();
    if (isset($repos[$this->FAIdebianRelease])) {
      $this->FAIdebianSection = $repos[$this->FAIdebianRelease];
      $this->FAIdebianSection = array_unique($this->FAIdebianSection);
    }
  }

  protected function shouldSave()
  {
    /* We need ldap_save to always run */
    return TRUE;
  }

  function ldap_save ()
  {
    global $config;
    $errors = parent::ldap_save();
    $this->attributesAccess['FAIpackage']->postLdapSave($config->get_ldap_link());
    return $errors;
  }

  function getServerInfos()
  {
    $servs    = serviceRepository::getServers();
    $ret      = array();
    foreach ($servs as $serv) {
      if (isset($ret[$serv['FAI_RELEASE']])) {
        $ret[$serv['FAI_RELEASE']] = array_merge($ret[$serv['FAI_RELEASE']], $serv['SECTIONS']);
      } else {
        $ret[$serv['FAI_RELEASE']] = $serv['SECTIONS'];
      }
    }
    return $ret;
  }
}
?>
