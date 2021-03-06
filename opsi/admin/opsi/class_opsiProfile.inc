<?php
/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2014-2016 FusionDirectory

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

class OPSIServerAttribute extends SelectAttribute
{
  function setParent (&$plugin)
  {
    parent::setParent($plugin);
    if (is_object($this->plugin)) {
      $this->updateChoices();
    }
  }

  function updateChoices()
  {
    global $config;
    $ldap = $config->get_ldap_link();
    $ldap->cd($config->current['BASE']);
    $ldap->search('(objectClass=opsiServer)', array('cn'));
    $serversdn    = array();
    $serverslabel = array();
    while ($attrs = $ldap->fetch()) {
      $serversdn[]    = $attrs['dn'];
      $serverslabel[] = $attrs['cn'][0];
    }
    $this->setChoices($serversdn, $serverslabel);
  }
}

class SubmittingOPSIServerAttribute extends OPSIServerAttribute
{
  function __construct ($label, $description, $ldapName, $required = FALSE, $acl = "")
  {
    parent::__construct($label, $description, $ldapName, $required, array(), "", NULL, $acl);
    $this->setSubmitForm('server_changed');
  }
}

class opsiProfile extends simplePlugin
{
  var $mainTab = TRUE;
  var $objectclasses   = array('opsiProfile');

  var $macAddress = NULL;
  var $products;

  public static function plInfo()
  {
    return array(
      'plShortName'   => _('OPSI profile'),
      'plDescription' => _('OPSI profile'),
      'plIcon'        => 'geticon.php?context=applications&icon=opsi&size=16',
      'plSelfModify'  => FALSE,
      'plObjectType'  => array('opsiProfile' => array(
        'name'    => _('OPSI profile'),
        'filter'  => 'objectClass=opsiProfile',
        'ou'      => get_ou('opsiRDN'),
        'icon'    => 'geticon.php?context=applications&icon=opsi&size=16',
      )),
      'plForeignKeys'   => array(
        'fdOpsiServerDn'      => 'serverGeneric',
        'fdOpsiSoftwareList'  => 'opsiSoftwareList',
      ),

      'plProvidedAcls' => parent::generatePlProvidedAcls(static::getAttributesInfo())
    );
  }

  static function getAttributesInfo ()
  {
    return array(
      'main' => array(
        'name'  => _('Name'),
        'attrs' => array(
          new BaseSelectorAttribute(get_ou('opsiRDN')),
          new SubmittingOPSIServerAttribute(
            _('OPSI server'), _('OPSI server to use for deployment'),
            'fdOpsiServerDn', TRUE
          ),
          new HostNameAttribute(
            _('Name'), _('Name of this OPSI profile'),
            'cn', TRUE
          ),
        )
      ),
      'products' => array(
        'name'  => _('Entries'),
        'attrs' => array(
          new SelectAttribute(
            _('Netboot product'), _('The netboot product to use for this profile'),
            'fdOpsiNetbootProduct', FALSE
          ),
          new SetAttribute(
            new SelectAttribute(
              _('Software lists'), _('Software lists that will be installed with this profile'),
              'fdOpsiSoftwareList', FALSE
            )
          ),
        )
      ),
    );
  }

  /* Returns list of all products installed by this profile */
  public function getProductList ()
  {
    global $config;
    $products = array();
    $netboot  = $this->fdOpsiNetbootProduct;
    if ($netboot != '') {
      $products[] = $netboot;
    }
    $lists = $this->fdOpsiSoftwareList;
    $ldap = $config->get_ldap_link();
    foreach ($lists as $list) {
      $ldap->cat($list);
      if ($attrs = $ldap->fetch()) {
        if (in_array('opsiOnDemandList', $attrs['objectClass'])) {
          continue;
        }
        for ($i = 0;$i < $attrs['fdOpsiLocalbootProduct']['count'];++$i) {
          $tmp = explode('|', $attrs['fdOpsiLocalbootProduct'][$i], 2);
          $products[] = $tmp[0];
        }
      } else {
        msg_dialog::display(
          sprintf(_("Could not find %s in the LDAP"), $this->fdOpsiServerDn),
          msgPool::ldaperror($ldap->get_error()), ERROR_DIALOG
        );
        return FALSE;
      }
    }
    return $products;
  }

  public function server_changed ()
  {
    global $config;
    if (!isset($this->products[$this->fdOpsiServerDn])) {
      $s_daemon = new supportDaemon();
      if (!$s_daemon->is_available()) {
        msg_dialog::display(
          _("Could not contact argonaut server"),
          msgPool::siError($s_daemon->get_error()), ERROR_DIALOG
        );
        return;
      }
      $ldap = $config->get_ldap_link();
      $ldap->cat($this->fdOpsiServerDn);
      if ($attrs = $ldap->fetch()) {
        if (isset($attrs['macAddress'])) {
          $this->macAddress = $attrs['macAddress'][0];
        } else {
          msg_dialog::display(
            _("No mac address"),
            sprintf(_("Server %s has no mac address configured in the LDAP"), $this->fdOpsiServerDn), ERROR_DIALOG
          );
          return FALSE;
        }
      } else {
        msg_dialog::display(
          sprintf(_('Could not find %s in the LDAP'), $this->fdOpsiServerDn),
          msgPool::ldaperror($ldap->get_error()), ERROR_DIALOG
        );
        return FALSE;
      }
      $netboots = $s_daemon->append_call('OPSI.get_netboots', $this->macAddress, array('args' => array(array('id','name'))));
      if ($s_daemon->is_error()) {
        msg_dialog::display(
          _('Failed to contact OPSI server'),
          msgPool::siError($s_daemon->get_error()), ERROR_DIALOG
        );
        return;
      }
      $this->products[$this->fdOpsiServerDn]['netboots'] = $netboots;

      $ldap->cd($config->current['BASE']);
      $ldap->search('(&(objectClass=opsiSoftwareList)(fdOpsiServerDn='.$this->fdOpsiServerDn.'))', array('cn'));
      $lists = array();
      while ($attrs = $ldap->fetch()) {
        $lists[$attrs['dn']] = $attrs['cn'][0];
      }
      $this->products[$this->fdOpsiServerDn]['lists'] = $lists;
    }
    $choices_values = array();
    $choices_labels = array();
    foreach ($this->products[$this->fdOpsiServerDn]['netboots'] as $netboot) {
      $choices_values[] = $netboot['id'];
      $choices_labels[] = sprintf(_('%s (%s)'), $netboot['id'], $netboot['name']);
    }
    $this->attributesAccess['fdOpsiNetbootProduct']->setChoices($choices_values, $choices_labels);
    $this->attributesAccess['fdOpsiSoftwareList']->attribute->setChoices(
      array_keys($this->products[$this->fdOpsiServerDn]['lists']),
      array_values($this->products[$this->fdOpsiServerDn]['lists'])
    );
  }
}
?>
